#include "Resource/PortraitVideoDecoder.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <vector>

namespace
{
    int Fail(const char* Message)
    {
        std::cerr << Message << "\n";
        return 1;
    }

    bool HasAlphaVariation(const Resource::Graphics::DecodedVideoFrame& Frame)
    {
        bool HasTransparent = false;
        bool HasOpaque = false;
        for (size_t I = 3; I < Frame.Image.Rgba.size(); I += 4)
        {
            HasTransparent = HasTransparent || Frame.Image.Rgba[I] < 255;
            HasOpaque = HasOpaque || Frame.Image.Rgba[I] > 0;
            if (HasTransparent && HasOpaque)
                return true;
        }
        return false;
    }

    bool AnyFrameHasAlphaVariation(const std::vector<Resource::Graphics::DecodedVideoFrame>& Frames)
    {
        return std::any_of(Frames.begin(), Frames.end(), [](const auto& Frame)
        {
            return HasAlphaVariation(Frame);
        });
    }

    bool StreamingDecoderStats(const std::filesystem::path& Path, size_t& OutFrameCount, bool& OutHasAlpha)
    {
        Resource::Graphics::WebmVideoDecoder Decoder;
        if (!Decoder.Open(Path))
            return false;

        Resource::Graphics::DecodedVideoFrame Frame;
        while (Decoder.ReadNextFrame(Frame))
        {
            ++OutFrameCount;
            OutHasAlpha = OutHasAlpha || HasAlphaVariation(Frame);
        }

        if (OutFrameCount == 0 || !Decoder.Rewind())
            return false;
        if (!Decoder.ReadNextFrame(Frame))
            return false;
        OutHasAlpha = OutHasAlpha || HasAlphaVariation(Frame);
        return true;
    }
}

int main()
{
    const std::filesystem::path PortraitPath = "assets/portrait/tadvisor/tadfid00.webm";
    auto PortraitFrames = Resource::Graphics::DecodeVideoFramesFromFile(PortraitPath);
    if (!PortraitFrames || PortraitFrames->empty())
        return Fail("failed to decode portrait webm");

    const auto& First = PortraitFrames->front();
    if (First.Image.Width != 448 || First.Image.Height != 480)
        return Fail("portrait webm decoded with unexpected dimensions");
    if (First.Image.Rgba.size() != (size_t)First.Image.Width * (size_t)First.Image.Height * 4u)
        return Fail("portrait webm rgba payload has unexpected size");
    if (First.DurationSeconds <= 0.0f)
        return Fail("portrait webm frame duration was not populated");
    const std::filesystem::path GluePath = "assets/glue/mainmenu/single.webm";
    auto GlueFrames = Resource::Graphics::DecodeVideoFramesFromFile(GluePath);
    if (!GlueFrames || GlueFrames->empty())
        return Fail("failed to decode glue webm");
    if (GlueFrames->front().Image.Width != 1700 || GlueFrames->front().Image.Height != 700)
        return Fail("glue webm decoded with unexpected dimensions");
    if (!AnyFrameHasAlphaVariation(*PortraitFrames) && !AnyFrameHasAlphaVariation(*GlueFrames))
        return Fail("webm alpha channel was not decoded");

    size_t StreamingPortraitFrameCount = 0;
    bool StreamingPortraitHasAlpha = false;
    if (!StreamingDecoderStats(PortraitPath, StreamingPortraitFrameCount, StreamingPortraitHasAlpha))
        return Fail("streaming webm decoder failed to read portrait frames");
    size_t StreamingGlueFrameCount = 0;
    bool StreamingGlueHasAlpha = false;
    if (!StreamingDecoderStats(GluePath, StreamingGlueFrameCount, StreamingGlueHasAlpha))
        return Fail("streaming webm decoder failed to read glue frames");
    if (!StreamingPortraitHasAlpha && !StreamingGlueHasAlpha)
        return Fail("streaming webm decoder did not expose alpha");

    std::cout << "decoded portrait frames=" << PortraitFrames->size()
              << " glue frames=" << GlueFrames->size()
              << " streaming portrait frames=" << StreamingPortraitFrameCount
              << " streaming glue frames=" << StreamingGlueFrameCount << "\n";
    return 0;
}
