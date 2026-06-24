#include "Resource/sokol/GrpToSokol.h"

#include "Core/Utils/Logger.h"

#include <cstring>

namespace Resource::SokolBridge
{

std::optional<DecodedSprite> DecodeGrpToRgba(
    const std::vector<uint8_t>& GrpData,
    const SokoPalette& Pal,
    const GrpDecodeOptions& Options)
{
    auto GrpOpt = GrpDecoder::DecodeGrp(GrpData);
    if (!GrpOpt) return std::nullopt;

    const GrpSprite& Sprite = *GrpOpt;
    const SokoPalette& ActivePalette = Options.PaletteOverride ? *Options.PaletteOverride : Pal;
    DecodedSprite Out;
    Out.Frames.reserve(Sprite.FrameCount);

    for (int I = 0; I < Sprite.FrameCount; ++I)
    {
        const GrpFrameInfo& Info = Sprite.Frames[I];
        DecodedFrame F;
        F.Width = Info.Width;
        F.Height = Info.Height;
        F.OriginX = 0;
        F.OriginY = 0;
        F.Flip = Info.Flip;
        F.Rgba.resize(static_cast<size_t>(Info.Width) * static_cast<size_t>(Info.Height) * 4, 0);

        int Px = Info.Width * Info.Height;
        for (int P = 0; P < Px; ++P)
        {
            uint8_t Idx = Sprite.RawFrameData[Info.Offset + P];
            if (Options.UseIndexRemap && Options.IndexRemap[Idx] >= 0)
                Idx = static_cast<uint8_t>(Options.IndexRemap[Idx]);

            const SokoColor& C = ActivePalette.Colors[Idx];
            const bool TransparentByIndex = Options.TransparentIndexZero && Idx == 0;
            const bool TransparentByBlack = Options.TransparentBlack &&
                                            C.R == Options.TransparentRgb.R &&
                                            C.G == Options.TransparentRgb.G &&
                                            C.B == Options.TransparentRgb.B;
            F.Rgba[P * 4 + 0] = C.R;
            F.Rgba[P * 4 + 1] = C.G;
            F.Rgba[P * 4 + 2] = C.B;
            F.Rgba[P * 4 + 3] = (TransparentByIndex || TransparentByBlack) ? 0 : C.A;
        }
        Out.Frames.push_back(std::move(F));
    }
    return Out;
}

sg_image UploadRgba(int Width, int Height, const uint8_t* Rgba)
{
    if (Width <= 0 || Height <= 0 || !Rgba) return sg_image{0};

    sg_image_desc Desc = {};
    Desc.width = Width;
    Desc.height = Height;
    Desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    Desc.data.mip_levels[0] = { Rgba, static_cast<size_t>(Width) * static_cast<size_t>(Height) * 4 };
    Desc.label = "sokol-grp-frame";

    sg_image Img = sg_make_image(&Desc);
    if (Img.id == 0)
    {
        Core::Utils::Logger::Error("GrpToSokol", "sg_make_image failed for " +
            std::to_string(Width) + "x" + std::to_string(Height));
    }
    return Img;
}

SokolImage UploadRgbaFull(int Width, int Height, const uint8_t* Rgba)
{
    SokolImage Out;
    Out.Width = Width;
    Out.Height = Height;
    Out.Image = UploadRgba(Width, Height, Rgba);
    if (Out.Image.id == 0) return Out;

    sg_view_desc VDesc = {};
    VDesc.texture.image = Out.Image;
    VDesc.label = "sokol-grp-view";
    Out.View = sg_make_view(&VDesc);
    if (Out.View.id == 0)
    {
        Core::Utils::Logger::Error("GrpToSokol", "sg_make_view failed");
        sg_destroy_image(Out.Image);
        Out.Image = sg_image{0};
    }
    return Out;
}

void DestroyImage(SokolImage& Img)
{
    if (Img.View.id != 0) { sg_destroy_view(Img.View); Img.View = sg_view{0}; }
    if (Img.Image.id != 0) { sg_destroy_image(Img.Image); Img.Image = sg_image{0}; }
    Img.Width = 0;
    Img.Height = 0;
}

} // namespace Resource::SokolBridge
