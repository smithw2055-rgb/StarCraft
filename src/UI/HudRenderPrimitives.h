#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Core/Color.h"
#include "Render/Draw2D.h"
#include "Resource/UIAssetTypes.h"
#include "UI/HudLayout.h"
#include "UI/HudText.h"

namespace Game::Client::UI
{

inline RectI InsetRect(const RectI& Rect, int InsetX, int InsetY)
{
    const int SafeInsetX = std::max(0, std::min(InsetX, std::max(0, Rect.W / 2 - 1)));
    const int SafeInsetY = std::max(0, std::min(InsetY, std::max(0, Rect.H / 2 - 1)));
    return
    {
        Rect.X + SafeInsetX,
        Rect.Y + SafeInsetY,
        std::max(1, Rect.W - SafeInsetX * 2),
        std::max(1, Rect.H - SafeInsetY * 2)
    };
}

inline void DrawContainedImage(const Resource::Graphics::GpuImage& Image,
                               const RectI& Rect,
                               Render::Color Tint)
{
    if (!Image.Valid() || Rect.W <= 0 || Rect.H <= 0 || Image.Width <= 0 || Image.Height <= 0)
        return;

    const float Scale = std::min((float)Rect.W / (float)Image.Width,
                                 (float)Rect.H / (float)Image.Height);
    const float DrawW = std::max(1.0f, std::floor((float)Image.Width * Scale));
    const float DrawH = std::max(1.0f, std::floor((float)Image.Height * Scale));
    const float DrawX = (float)Rect.X + ((float)Rect.W - DrawW) * 0.5f;
    const float DrawY = (float)Rect.Y + ((float)Rect.H - DrawH) * 0.5f;
    Render::g_Draw2D.DrawSpriteFromImage(Image, DrawX, DrawY, DrawW, DrawH, Tint);
}

inline void DrawAtlasImage(const Resource::Graphics::GpuImage& Image,
                           const AtlasRect& SourceRect,
                           const RectI& DestRect,
                           Render::Color Tint)
{
    if (!Image.Valid() || Image.Width <= 0 || Image.Height <= 0 ||
        SourceRect.W <= 0 || SourceRect.H <= 0 ||
        DestRect.W <= 0 || DestRect.H <= 0)
    {
        return;
    }

    const float U0 = (float)SourceRect.X / (float)Image.Width;
    const float V0 = (float)SourceRect.Y / (float)Image.Height;
    const float U1 = (float)(SourceRect.X + SourceRect.W) / (float)Image.Width;
    const float V1 = (float)(SourceRect.Y + SourceRect.H) / (float)Image.Height;
    Render::g_Draw2D.DrawSprite(Image,
                                (float)DestRect.X, (float)DestRect.Y,
                                (float)DestRect.W, (float)DestRect.H,
                                U0, V0, U1, V1, Tint, false);
}

inline void DrawContainedAtlas(const Resource::Graphics::GpuImage& Image,
                               const AtlasRect& SourceRect,
                               const RectI& Rect,
                               Render::Color Tint)
{
    if (!Image.Valid() || Rect.W <= 0 || Rect.H <= 0 || SourceRect.W <= 0 || SourceRect.H <= 0)
        return;

    const float Scale = std::min((float)Rect.W / (float)SourceRect.W,
                                 (float)Rect.H / (float)SourceRect.H);
    const int DrawW = std::max(1, (int)std::floor((float)SourceRect.W * Scale));
    const int DrawH = std::max(1, (int)std::floor((float)SourceRect.H * Scale));
    const RectI Dest =
    {
        Rect.X + (Rect.W - DrawW) / 2,
        Rect.Y + (Rect.H - DrawH) / 2,
        DrawW,
        DrawH
    };
    DrawAtlasImage(Image, SourceRect, Dest, Tint);
}

inline RectI ExpandRectFromOpening(const RectI& OpeningRect,
                                   int SourceWidth,
                                   int SourceHeight,
                                   const RectI& OpeningInSource)
{
    if (OpeningRect.W <= 0 || OpeningRect.H <= 0 || SourceWidth <= 0 || SourceHeight <= 0 ||
        OpeningInSource.W <= 0 || OpeningInSource.H <= 0)
    {
        return OpeningRect;
    }

    const float ScaleX = (float)OpeningRect.W / (float)OpeningInSource.W;
    const float ScaleY = (float)OpeningRect.H / (float)OpeningInSource.H;
    const int OuterW = std::max(1, (int)std::lround((float)SourceWidth * ScaleX));
    const int OuterH = std::max(1, (int)std::lround((float)SourceHeight * ScaleY));
    return
    {
        OpeningRect.X - (int)std::lround((float)OpeningInSource.X * ScaleX),
        OpeningRect.Y - (int)std::lround((float)OpeningInSource.Y * ScaleY),
        OuterW,
        OuterH
    };
}

inline RectI ComputeCommandIconRect(const RectI& ButtonRect)
{
    const int InsetX = std::max(4, ButtonRect.W / 14);
    const int InsetY = std::max(4, ButtonRect.H / 14);
    const int BottomReserve = 0;
    return
    {
        ButtonRect.X + InsetX,
        ButtonRect.Y + InsetY,
        std::max(8, ButtonRect.W - InsetX * 2),
        std::max(8, ButtonRect.H - InsetY * 2 - BottomReserve)
    };
}

inline void DrawNineSlice(const Resource::Graphics::GpuImage& Image,
                          const RectI& DestRect,
                          int BorderLeft,
                          int BorderTop,
                          int BorderRight,
                          int BorderBottom,
                          Render::Color Tint)
{
    if (!Image.Valid() || DestRect.W <= 0 || DestRect.H <= 0 || Image.Width <= 0 || Image.Height <= 0)
        return;

    const int SrcLeft = std::clamp(BorderLeft, 0, std::max(0, Image.Width - 2));
    const int SrcTop = std::clamp(BorderTop, 0, std::max(0, Image.Height - 2));
    const int SrcRight = std::clamp(BorderRight, 0, std::max(0, Image.Width - SrcLeft - 1));
    const int SrcBottom = std::clamp(BorderBottom, 0, std::max(0, Image.Height - SrcTop - 1));
    const int CenterSrcW = std::max(1, Image.Width - SrcLeft - SrcRight);
    const int CenterSrcH = std::max(1, Image.Height - SrcTop - SrcBottom);

    int DstLeft = std::min(DestRect.W / 2, std::max(1, (int)std::lround((double)SrcLeft * (double)DestRect.H / (double)Image.Height)));
    int DstRight = std::min(DestRect.W - DstLeft, std::max(1, (int)std::lround((double)SrcRight * (double)DestRect.H / (double)Image.Height)));
    int DstTop = std::min(DestRect.H / 2, std::max(1, (int)std::lround((double)SrcTop * (double)DestRect.H / (double)Image.Height)));
    int DstBottom = std::min(DestRect.H - DstTop, std::max(1, (int)std::lround((double)SrcBottom * (double)DestRect.H / (double)Image.Height)));
    if (DstLeft + DstRight >= DestRect.W)
    {
        DstLeft = std::max(1, DestRect.W / 2);
        DstRight = std::max(1, DestRect.W - DstLeft);
    }
    if (DstTop + DstBottom >= DestRect.H)
    {
        DstTop = std::max(1, DestRect.H / 2);
        DstBottom = std::max(1, DestRect.H - DstTop);
    }

    const int DstCenterW = std::max(1, DestRect.W - DstLeft - DstRight);
    const int DstCenterH = std::max(1, DestRect.H - DstTop - DstBottom);

    const AtlasRect TopLeft = { 0, 0, SrcLeft, SrcTop };
    const AtlasRect TopCenter = { SrcLeft, 0, CenterSrcW, SrcTop };
    const AtlasRect TopRight = { SrcLeft + CenterSrcW, 0, SrcRight, SrcTop };
    const AtlasRect MidLeft = { 0, SrcTop, SrcLeft, CenterSrcH };
    const AtlasRect MidCenter = { SrcLeft, SrcTop, CenterSrcW, CenterSrcH };
    const AtlasRect MidRight = { SrcLeft + CenterSrcW, SrcTop, SrcRight, CenterSrcH };
    const AtlasRect BottomLeft = { 0, SrcTop + CenterSrcH, SrcLeft, SrcBottom };
    const AtlasRect BottomCenter = { SrcLeft, SrcTop + CenterSrcH, CenterSrcW, SrcBottom };
    const AtlasRect BottomRight = { SrcLeft + CenterSrcW, SrcTop + CenterSrcH, SrcRight, SrcBottom };

    const int X0 = DestRect.X;
    const int X1 = DestRect.X + DstLeft;
    const int X2 = DestRect.X + DestRect.W - DstRight;
    const int Y0 = DestRect.Y;
    const int Y1 = DestRect.Y + DstTop;
    const int Y2 = DestRect.Y + DestRect.H - DstBottom;

    DrawAtlasImage(Image, TopLeft, { X0, Y0, DstLeft, DstTop }, Tint);
    DrawAtlasImage(Image, TopCenter, { X1, Y0, DstCenterW, DstTop }, Tint);
    DrawAtlasImage(Image, TopRight, { X2, Y0, DstRight, DstTop }, Tint);
    DrawAtlasImage(Image, MidLeft, { X0, Y1, DstLeft, DstCenterH }, Tint);
    DrawAtlasImage(Image, MidCenter, { X1, Y1, DstCenterW, DstCenterH }, Tint);
    DrawAtlasImage(Image, MidRight, { X2, Y1, DstRight, DstCenterH }, Tint);
    DrawAtlasImage(Image, BottomLeft, { X0, Y2, DstLeft, DstBottom }, Tint);
    DrawAtlasImage(Image, BottomCenter, { X1, Y2, DstCenterW, DstBottom }, Tint);
    DrawAtlasImage(Image, BottomRight, { X2, Y2, DstRight, DstBottom }, Tint);
}

inline void DrawConsoleSlices(const Resource::Graphics::GpuImage& Image,
                              const RectI& DestRect,
                              Render::Color Tint)
{
    constexpr int ClassicConsoleSourceWidth = 640;
    constexpr int ClassicConsoleLeftSrcWidth = 176;
    constexpr int ClassicConsoleRightSrcWidth = 216;

    if (!Image.Valid() || DestRect.W <= 0 || DestRect.H <= 0 || Image.Width <= 0 || Image.Height <= 0)
        return;

    const float SourceWidth = (float)Image.Width;
    const float SourceHeight = (float)Image.Height;
    if (SourceWidth <= 1.0f || SourceHeight <= 1.0f)
        return;

    const float Scale = (float)DestRect.H / SourceHeight;
    const float LeftSrcW = SourceWidth * ((float)ClassicConsoleLeftSrcWidth / (float)ClassicConsoleSourceWidth);
    const float RightSrcW = SourceWidth * ((float)ClassicConsoleRightSrcWidth / (float)ClassicConsoleSourceWidth);
    const float CenterSrcW = std::max(0.0f, SourceWidth - LeftSrcW - RightSrcW);
    if (CenterSrcW <= 1.0f)
    {
        Render::g_Draw2D.DrawSpriteFromImage(Image,
                                             (float)DestRect.X, (float)DestRect.Y,
                                             (float)DestRect.W, (float)DestRect.H,
                                             Tint);
        return;
    }

    float LeftDstW = LeftSrcW * Scale;
    float RightDstW = RightSrcW * Scale;
    if (LeftDstW + RightDstW > (float)DestRect.W)
    {
        const float Shrink = (float)DestRect.W / (LeftDstW + RightDstW);
        LeftDstW *= Shrink;
        RightDstW *= Shrink;
    }
    const float CenterDstW = std::max(0.0f, (float)DestRect.W - LeftDstW - RightDstW);

    Render::g_Draw2D.DrawSprite(Image,
                                (float)DestRect.X, (float)DestRect.Y,
                                LeftDstW, (float)DestRect.H,
                                0.0f, 0.0f, LeftSrcW / SourceWidth, 1.0f,
                                Tint, false);

    if (CenterDstW > 0.0f)
    {
        Render::g_Draw2D.DrawSprite(Image,
                                    (float)DestRect.X + LeftDstW, (float)DestRect.Y,
                                    CenterDstW, (float)DestRect.H,
                                    LeftSrcW / SourceWidth, 0.0f, (LeftSrcW + CenterSrcW) / SourceWidth, 1.0f,
                                    Tint, false);
    }

    Render::g_Draw2D.DrawSprite(Image,
                                (float)DestRect.X + LeftDstW + CenterDstW, (float)DestRect.Y,
                                RightDstW, (float)DestRect.H,
                                (LeftSrcW + CenterSrcW) / SourceWidth, 0.0f, 1.0f, 1.0f,
                                Tint, false);
}

} // namespace Game::Client::UI

// === Consolidated from HudButtonStyle.h & HudInfoWidgets.h ===
#include <cstddef>
#include <vector>

#include "Core/Color.h"
#include "Resource/UIAssetTypes.h"
#include "UI/HudState.h"

namespace Game::Client::UI
{

inline Resource::Graphics::CommandButtonSkinSet SkinSetForPanelCategory(int CategoryValue)
{
    using Resource::Graphics::CommandButtonSkinSet;
    switch (CategoryValue)
    {
        case 3: return CommandButtonSkinSet::Protoss;
        case 2: return CommandButtonSkinSet::Zerg;
        case 1:
        case 0:
        default:
            return CommandButtonSkinSet::Terran;
    }
}

inline const Resource::Graphics::GpuImage* FindPreferredButtonSkin(
    const std::vector<Resource::Graphics::GpuImage>* Skins,
    int PreferredIndex)
{
    if (!Skins || Skins->empty())
        return nullptr;

    if (PreferredIndex >= 0 &&
        PreferredIndex < static_cast<int>(Skins->size()) &&
        (*Skins)[static_cast<size_t>(PreferredIndex)].Valid())
    {
        return &(*Skins)[static_cast<size_t>(PreferredIndex)];
    }

    for (const auto& Skin : *Skins)
    {
        if (Skin.Valid())
            return &Skin;
    }
    return nullptr;
}

inline bool IsNeutralButtonIcon(const CmdButtonDef& Button)
{
    return Button.CommandId == HudCommandId::Stop || Button.Kind == ActionKind::Back;
}

inline Render::Color SkinTintForState(bool HasCommand, bool Enabled, bool Hovered, bool Active)
{
    if (!HasCommand)
        return Render::Color(90, 90, 96, 80);
    if (!Enabled)
        return Render::Color(118, 128, 140, 185);
    if (Active)
        return Render::Color(255, 255, 255, 255);
    if (Hovered)
        return Render::Color(232, 240, 255, 250);
    return Render::Colors::White();
}

inline Render::Color IconTintForState(const CmdButtonDef& Button, bool Enabled, bool Hovered, bool Active)
{
    const bool Neutral = IsNeutralButtonIcon(Button);
    if (!Enabled)
        return Neutral ? Render::Color(125, 128, 132, 205) : Render::Color(124, 118, 82, 205);
    if (Neutral)
        return Active ? Render::Color(255, 255, 255, 255)
                      : (Hovered ? Render::Color(245, 245, 248, 255)
                                 : Render::Color(230, 230, 236, 255));
    if (Active)
        return Render::Color(255, 246, 138, 255);
    if (Hovered)
        return Render::Color(255, 239, 108, 255);
    return Render::Color(245, 226, 78, 255);
}

inline Render::Color FallbackIconTintForState(bool Enabled, bool Active)
{
    if (!Enabled)
        return Render::Color(145, 145, 145, 190);
    if (Active)
        return Render::Color(255, 250, 205, 255);
    return Render::Colors::White();
}

} // namespace Game::Client::UI

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Core/Color.h"
#include "Resource/UIAssetTypes.h"
#include "UI/HudLayout.h"

#include "UI/HudText.h"

namespace Game::Client::UI
{

inline void DrawInfoFooterCard(const RectI& CardRect,
                               const std::string& Text,
                               Render::Color Tint,
                               const Resource::Graphics::GpuImage& AggIcons,
                               const AtlasRect* ResourceIcon = nullptr,
                               const Resource::Graphics::GpuImage* CommandMask = nullptr)
{
    if (Text.empty())
        return;

    auto& D = Render::g_Draw2D;
    D.DrawRect((float)CardRect.X, (float)CardRect.Y,
               (float)CardRect.W, (float)CardRect.H,
               Render::Color(24, 24, 32, 230));
    D.DrawRectLines((float)CardRect.X, (float)CardRect.Y,
                    (float)CardRect.W, (float)CardRect.H,
                    Render::Color(100, 120, 110, 150), 1.0f);
    const RectI IconRect = { CardRect.X + 5, CardRect.Y + 7, 14, 14 };
    if (ResourceIcon && AggIcons.Valid())
        DrawAtlasImage(AggIcons, *ResourceIcon, IconRect, Render::Colors::White());
    else if (CommandMask && CommandMask->Valid())
        DrawContainedImage(*CommandMask, IconRect, Tint);

    D.DrawText(Ellipsize(Text, std::max<size_t>(8, (CardRect.W - 24) / 7)),
               (float)CardRect.X + 23.0f, (float)CardRect.Y + 7.0f, 1.0f, Tint);
}

inline int DrawInfoMiniStat(int X,
                            const RectI& HeaderRect,
                            const Resource::Graphics::GpuImage* Icon,
                            const Resource::Graphics::GpuImage* Mask,
                            const std::string& Value,
                            Render::Color Tint)
{
    const RectI IconRect = { X, HeaderRect.Y + HeaderRect.H - 16, 14, 14 };
    if (Icon && Icon->Valid())
        DrawContainedImage(*Icon, IconRect, Render::Colors::White());
    else if (Mask && Mask->Valid())
        DrawContainedImage(*Mask, IconRect, Tint);
    Render::g_Draw2D.DrawText(Value, (float)X + 18.0f, (float)HeaderRect.Y + (float)HeaderRect.H - 13.0f, 0.95f,
                              Render::Color(215, 220, 225, 255));
    return X + 18 + std::max(18, (int)Value.size() * 8) + 12;
}

inline void DrawInfoValueBar(int& BarY,
                             const char* Label,
                             int Value,
                             int MaxValue,
                             const RectI& StatBodyRect,
                             int FooterY,
                             const Resource::Graphics::GpuImage& HpEmptyStrip,
                             const Resource::Graphics::GpuImage& HpStrip,
                             Render::Color FillColor)
{
    if (MaxValue <= 0 || BarY + 8 > FooterY + 20)
        return;

    auto& D = Render::g_Draw2D;
    const int BarLabelW = 24;
    const int TextNumberW = 58;
    const int BarW = std::max(40, StatBodyRect.W - BarLabelW - TextNumberW - 4);
    const float Frac = std::clamp((float)Value / (float)MaxValue, 0.0f, 1.0f);
    D.DrawText(Label, (float)StatBodyRect.X, (float)BarY - 1.0f, 0.95f, Render::Color(180, 180, 190, 255));
    D.DrawRect((float)StatBodyRect.X + (float)BarLabelW, (float)BarY, (float)BarW, 8.0f,
               Render::Color(28, 28, 38, 230));

    const bool UseHpSkin = std::strcmp(Label, "HP") == 0;
    if (HpEmptyStrip.Valid() && UseHpSkin)
    {
        DrawAtlasImage(HpEmptyStrip, { 0, 0, HpEmptyStrip.Width, HpEmptyStrip.Height },
                       { StatBodyRect.X + BarLabelW, BarY, BarW, 8 },
                       Render::Colors::White());
    }
    if (HpStrip.Valid() && UseHpSkin)
    {
        const int FilledW = std::max(1, (int)std::floor((float)BarW * Frac));
        const int SrcW = std::clamp((int)std::floor((float)HpStrip.Width * Frac), 1, HpStrip.Width);
        DrawAtlasImage(HpStrip, { 0, 0, SrcW, HpStrip.Height },
                       { StatBodyRect.X + BarLabelW, BarY, FilledW, 8 },
                       Render::Colors::White());
    }
    else
    {
        D.DrawRect((float)StatBodyRect.X + (float)BarLabelW, (float)BarY, (float)BarW * Frac, 8.0f, FillColor);
    }
    D.DrawRectLines((float)StatBodyRect.X + (float)BarLabelW, (float)BarY, (float)BarW, 8.0f,
                    Render::Color(100, 100, 110, 180), 1.0f);

    char Buf[64];
    std::snprintf(Buf, sizeof(Buf), "%d/%d", Value, MaxValue);
    D.DrawText(Buf, (float)StatBodyRect.X + (float)BarLabelW + (float)BarW + 5.0f, (float)BarY - 1.0f, 0.95f,
               Render::Color(220, 220, 230, 255));
    BarY += 13;
}

inline void DrawInfoProgressBar(int& BarY,
                                float Progress,
                                const RectI& StatBodyRect,
                                Render::Color FillColor)
{
    auto& D = Render::g_Draw2D;
    Progress = std::clamp(Progress, 0.0f, 1.0f);
    const int BarLabelW = 24;
    const int TextNumberW = 40;
    const int BarW = std::max(40, StatBodyRect.W - BarLabelW - TextNumberW - 4);
    D.DrawText("PR", (float)StatBodyRect.X, (float)BarY - 1.0f, 0.95f, Render::Color(180, 180, 190, 255));
    D.DrawRect((float)StatBodyRect.X + (float)BarLabelW, (float)BarY, (float)BarW, 8.0f,
               Render::Color(28, 28, 38, 230));
    D.DrawRect((float)StatBodyRect.X + (float)BarLabelW, (float)BarY, (float)BarW * Progress, 8.0f,
               FillColor);
    D.DrawRectLines((float)StatBodyRect.X + (float)BarLabelW, (float)BarY, (float)BarW, 8.0f,
                    Render::Color(100, 100, 110, 180), 1.0f);

    char Buf[64];
    std::snprintf(Buf, sizeof(Buf), "%d%%", (int)std::lround(Progress * 100.0f));
    D.DrawText(Buf, (float)StatBodyRect.X + (float)BarLabelW + (float)BarW + 5.0f, (float)BarY - 1.0f, 0.95f,
               Render::Color(220, 220, 230, 255));
    BarY += 13;
}

inline void DrawInfoQueueList(const RectI& QueueRect,
                              const std::string& QueueLabel,
                              const std::vector<std::string>& QueueEntries)
{
    if (QueueEntries.empty())
        return;

    auto& D = Render::g_Draw2D;
    D.DrawText(QueueLabel.empty() ? "Queue" : QueueLabel,
               (float)QueueRect.X, (float)QueueRect.Y - 12.0f, 1.1f,
               Render::Color(200, 200, 180, 255));

    constexpr int QueueEntryHeight = 18;
    for (size_t I = 0; I < QueueEntries.size() && I < 5; ++I)
    {
        const int EntryY = QueueRect.Y + (int)I * (QueueEntryHeight + 4);
        D.DrawRect((float)QueueRect.X, (float)EntryY, (float)QueueRect.W, (float)QueueEntryHeight,
                   I == 0 ? Render::Color(70, 58, 28, 220) : Render::Color(26, 26, 38, 220));
        D.DrawRectLines((float)QueueRect.X, (float)EntryY, (float)QueueRect.W, (float)QueueEntryHeight,
                        Render::Color(110, 110, 120, 180), 1.0f);
        D.DrawText(Ellipsize(QueueEntries[I], std::max<size_t>(10, QueueRect.W / 7)),
                   (float)QueueRect.X + 4.0f, (float)EntryY + 4.0f, 1.0f,
                   Render::Color(225, 225, 225, 255));
    }
}

} // namespace Game::Client::UI