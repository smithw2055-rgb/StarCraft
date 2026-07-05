#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "Resource/RgbaImage.h"

namespace Resource::Graphics
{

struct DecodedVideoFrame
{
    DecodedRgbaImage Image;
    float DurationSeconds = 1.0f / 12.0f;
};

class WebmVideoDecoder
{
public:
    WebmVideoDecoder();
    ~WebmVideoDecoder();

    WebmVideoDecoder(WebmVideoDecoder&&) noexcept;
    WebmVideoDecoder& operator=(WebmVideoDecoder&&) noexcept;

    WebmVideoDecoder(const WebmVideoDecoder&) = delete;
    WebmVideoDecoder& operator=(const WebmVideoDecoder&) = delete;

    bool Open(const std::filesystem::path& Path);
    void Close();
    bool Rewind();
    bool ReadNextFrame(DecodedVideoFrame& OutFrame);

    bool Valid() const;
    int Width() const;
    int Height() const;
    size_t EncodedFrameCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

std::optional<std::vector<DecodedVideoFrame>> DecodeVideoFramesFromFile(const std::filesystem::path& Path);

} // namespace Resource::Graphics
