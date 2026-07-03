#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Render/Color.h"
#include "Resource/GpuImage.h"
#include "UI/HudLayout.h"
#include "UI/HudRenderPrimitives.h"
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
    if (MaxValue <= 0 || BarY + 8 > FooterY - 6)
        return;

    auto& D = Render::g_Draw2D;
    const int BarLabelW = 30;
    const int BarW = std::max(64, StatBodyRect.W - BarLabelW - 8);
    const float Frac = std::clamp((float)Value / (float)MaxValue, 0.0f, 1.0f);
    D.DrawText(Label, (float)StatBodyRect.X, (float)BarY - 1.0f, 1.0f, Render::Color(180, 180, 190, 255));
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
        DrawAtlasImage(HpStrip, { 0, 0, HpStrip.Width, HpStrip.Height },
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
    D.DrawText(Buf, (float)StatBodyRect.X + (float)BarLabelW + (float)BarW - 48.0f, (float)BarY - 1.0f, 1.0f,
               Render::Color(220, 220, 230, 255));
    BarY += 14;
}

inline void DrawInfoProgressBar(int& BarY,
                                float Progress,
                                const RectI& StatBodyRect,
                                Render::Color FillColor)
{
    auto& D = Render::g_Draw2D;
    Progress = std::clamp(Progress, 0.0f, 1.0f);
    const int BarLabelW = 30;
    const int BarW = std::max(64, StatBodyRect.W - BarLabelW - 8);
    D.DrawText("PR", (float)StatBodyRect.X, (float)BarY - 1.0f, 1.0f, Render::Color(180, 180, 190, 255));
    D.DrawRect((float)StatBodyRect.X + (float)BarLabelW, (float)BarY, (float)BarW, 8.0f,
               Render::Color(28, 28, 38, 230));
    D.DrawRect((float)StatBodyRect.X + (float)BarLabelW, (float)BarY, (float)BarW * Progress, 8.0f,
               FillColor);
    D.DrawRectLines((float)StatBodyRect.X + (float)BarLabelW, (float)BarY, (float)BarW, 8.0f,
                    Render::Color(100, 100, 110, 180), 1.0f);

    char Buf[64];
    std::snprintf(Buf, sizeof(Buf), "%d%%", (int)std::lround(Progress * 100.0f));
    D.DrawText(Buf, (float)StatBodyRect.X + (float)BarLabelW + (float)BarW - 34.0f, (float)BarY - 1.0f, 1.0f,
               Render::Color(220, 220, 230, 255));
    BarY += 14;
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