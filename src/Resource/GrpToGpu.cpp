#include "Resource/GrpToGpu.h"
#include "Render/Private/SokolGpuInterop.h"

#include "Core/Utils/Logger.h"

#include <cstring>

namespace Resource::Graphics
{

namespace
{
    bool IsTeamColorIndex(uint8_t Index, const GrpDecodeOptions& Options)
    {
        if (!Options.GenerateTeamColorMask || Options.TeamColorIndexCount == 0)
            return false;

        const int Start = Options.TeamColorIndexStart;
        const int End = Start + Options.TeamColorIndexCount;
        return Index >= Start && Index < End;
    }
}

std::optional<DecodedSprite> DecodeGrpToRgba(
    const std::vector<uint8_t>& GrpData,
    const ColorPalette& Pal,
    const GrpDecodeOptions& Options)
{
    auto GrpOpt = GrpDecoder::DecodeGrp(GrpData);
    if (!GrpOpt) return std::nullopt;

    const GrpSprite& Sprite = *GrpOpt;
    const ColorPalette& ActivePalette = Options.PaletteOverride ? *Options.PaletteOverride : Pal;
    DecodedSprite Out;
    Out.Frames.reserve(Sprite.FrameCount);

    for (int I = 0; I < Sprite.FrameCount; ++I)
    {
        const GrpFrameInfo& Info = Sprite.Frames[I];
        DecodedFrame F;
        F.Width = Sprite.Width > 0 ? Sprite.Width : Info.Width;
        F.Height = Sprite.Height > 0 ? Sprite.Height : Info.Height;
        F.OriginX = 0;
        F.OriginY = 0;
        F.Flip = Info.Flip;
        F.Rgba.resize(static_cast<size_t>(F.Width) * static_cast<size_t>(F.Height) * 4, 0);
        F.TeamColorMaskRgba.resize(static_cast<size_t>(F.Width) * static_cast<size_t>(F.Height) * 4, 0);
        bool HasTeamColorMask = false;

        int Px = Info.Width * Info.Height;
        for (int P = 0; P < Px; ++P)
        {
            const uint8_t SourceIndex = Sprite.RawFrameData[Info.Offset + P];
            uint8_t Idx = SourceIndex;
            const bool TeamColorPixel = IsTeamColorIndex(SourceIndex, Options);
            if (Options.UseIndexRemap && Options.IndexRemap[Idx] >= 0)
                Idx = static_cast<uint8_t>(Options.IndexRemap[Idx]);

            const SokoColor& C = ActivePalette.Colors[Idx];
            const bool TransparentByIndex = Options.TransparentIndexZero && Idx == 0;
            const bool TransparentByBlack = Options.TransparentBlack &&
                                            C.R == Options.TransparentRgb.R &&
                                            C.G == Options.TransparentRgb.G &&
                                            C.B == Options.TransparentRgb.B;
            const int LocalX = P % Info.Width;
            const int LocalY = P / Info.Width;
            const int OutX = Info.XOffset + LocalX;
            const int OutY = Info.YOffset + LocalY;
            if (OutX < 0 || OutY < 0 || OutX >= F.Width || OutY >= F.Height)
                continue;

            const size_t OutOffset = ((size_t)OutY * (size_t)F.Width + (size_t)OutX) * 4u;
            if (Options.DecodeLightTableMask)
            {
                const uint8_t Alpha = DecodeLightTableMaskAlpha(
                    SourceIndex, Options.LightTableIndexCount,
                    Options.LightTableMaxAlpha);
                const unsigned Strength = Options.LightTableIndexCount > 1
                    ? (static_cast<unsigned>(std::min<uint8_t>(
                           SourceIndex, Options.LightTableIndexCount) -
                       (SourceIndex > 0 ? 1u : 0u)) * 255u) /
                          static_cast<unsigned>(Options.LightTableIndexCount - 1)
                    : 0u;
                F.Rgba[OutOffset + 0] = static_cast<uint8_t>(
                    Options.LightTableTint.R +
                    ((255u - Options.LightTableTint.R) * Strength) / 255u);
                F.Rgba[OutOffset + 1] = static_cast<uint8_t>(
                    Options.LightTableTint.G +
                    ((255u - Options.LightTableTint.G) * Strength) / 255u);
                F.Rgba[OutOffset + 2] = static_cast<uint8_t>(
                    Options.LightTableTint.B +
                    ((255u - Options.LightTableTint.B) * Strength) / 255u);
                F.Rgba[OutOffset + 3] = Alpha;
            }
            else
            {
                F.Rgba[OutOffset + 0] = C.R;
                F.Rgba[OutOffset + 1] = C.G;
                F.Rgba[OutOffset + 2] = C.B;
                F.Rgba[OutOffset + 3] =
                    (TransparentByIndex || TransparentByBlack) ? 0 : C.A;
            }

            if (TeamColorPixel && F.Rgba[OutOffset + 3] != 0)
            {
                F.TeamColorMaskRgba[OutOffset + 0] = 255;
                F.TeamColorMaskRgba[OutOffset + 1] = 255;
                F.TeamColorMaskRgba[OutOffset + 2] = 255;
                F.TeamColorMaskRgba[OutOffset + 3] = 255;
                HasTeamColorMask = true;
            }
        }
        if (!HasTeamColorMask)
            F.TeamColorMaskRgba.clear();
        Out.Frames.push_back(std::move(F));
    }
    return Out;
}

static sg_image UploadRgba(int Width, int Height, const uint8_t* Rgba)
{
    if (Width <= 0 || Height <= 0 || !Rgba) return sg_image{0};

    sg_image_desc Desc = {};
    Desc.width = Width;
    Desc.height = Height;
    Desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    Desc.data.mip_levels[0] = { Rgba, static_cast<size_t>(Width) * static_cast<size_t>(Height) * 4 };
    Desc.label = "grp-frame";

    sg_image Img = sg_make_image(&Desc);
    if (Img.id == 0)
    {
        Core::Utils::Logger::Error("GrpToGpu", "sg_make_image failed for " +
            std::to_string(Width) + "x" + std::to_string(Height));
    }
    return Img;
}

GpuImage UploadRgbaFull(int Width, int Height, const uint8_t* Rgba)
{
    GpuImage Out;
    Out.Width = Width;
    Out.Height = Height;
    if (Rgba && Width > 0 && Height > 0)
    {
        const size_t PixelCount = static_cast<size_t>(Width) * static_cast<size_t>(Height);
        for (size_t I = 0; I < PixelCount; ++I)
        {
            if (Rgba[I * 4u + 3u] != 0)
            {
                Out.HasVisiblePixels = true;
                break;
            }
        }
    }
    Out.Image = Render::FromSokol(UploadRgba(Width, Height, Rgba));
    if (Out.Image.id == 0) return Out;

    sg_view_desc VDesc = {};
    VDesc.texture.image = Render::ToSokol(Out.Image);
    VDesc.label = "grp-view";
    Out.View = Render::FromSokol(sg_make_view(&VDesc));
    if (Out.View.id == 0)
    {
        Core::Utils::Logger::Error("GrpToGpu", "sg_make_view failed");
        sg_destroy_image(Render::ToSokol(Out.Image));
        Out.Image = {};
    }
    return Out;
}

void DestroyImage(GpuImage& Img)
{
    if (Img.TeamColorMask.View.id != 0)
    {
        sg_destroy_view(Render::ToSokol(Img.TeamColorMask.View));
        Img.TeamColorMask.View = {};
    }
    if (Img.TeamColorMask.Image.id != 0)
    {
        sg_destroy_image(Render::ToSokol(Img.TeamColorMask.Image));
        Img.TeamColorMask.Image = {};
    }
    if (Img.View.id != 0) { sg_destroy_view(Render::ToSokol(Img.View)); Img.View = {}; }
    if (Img.Image.id != 0) { sg_destroy_image(Render::ToSokol(Img.Image)); Img.Image = {}; }
    Img.Width = 0;
    Img.Height = 0;
}

} // namespace Resource::Graphics
