#include "Resource/ResourceCache.h"

#include "Core/Utils/Logger.h"

#include "Resource/GrpToGpu.h"
#include "Resource/PortraitVideoDecoder.h"
#include "Resource/RgbaImageFileDecoder.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Resource::Graphics
{

namespace
{
    std::string ToLowerCopy(const std::string& Value)
    {
        std::string Lower = Value;
        std::transform(Lower.begin(), Lower.end(), Lower.begin(),
            [](unsigned char C) { return (char)std::tolower(C); });
        return Lower;
    }
}

const PortraitClip* ResourceCache::GetPortraitClip(const std::string& PortraitKey)
{
    if (PortraitKey.empty())
        return nullptr;

    const std::string Key = ToLowerCopy(PortraitKey);
    if (auto It = mUI.PortraitClips.find(Key); It != mUI.PortraitClips.end())
        return It->second.Valid() ? &It->second : nullptr;

    PortraitClip Clip = {};
    if (!LoadPortraitClip(Key, Clip))
    {
        mUI.PortraitClips.emplace(Key, PortraitClip{});
        return nullptr;
    }

    auto [It, Inserted] = mUI.PortraitClips.emplace(Key, std::move(Clip));
    (void)Inserted;
    return It->second.Valid() ? &It->second : nullptr;
}

bool ResourceCache::LoadPortraitClip(const std::string& PortraitKey, PortraitClip& OutClip)
{
#ifdef _WIN32
    if (!mSources.UseExtractedAssets || mSources.AssetsRoot.empty())
        return false;

    std::filesystem::path PortraitDir = std::filesystem::path(mSources.AssetsRoot) / "portrait" / PortraitKey;
    if (!std::filesystem::exists(PortraitDir) || !std::filesystem::is_directory(PortraitDir))
        return false;

    std::vector<std::filesystem::path> Clips;
    for (const auto& Entry : std::filesystem::directory_iterator(PortraitDir))
    {
        if (!Entry.is_regular_file())
            continue;

        const std::string FileName = ToLowerCopy(Entry.path().filename().string());
        if (ToLowerCopy(Entry.path().extension().string()) == ".webm" &&
            FileName.find("fid") != std::string::npos)
            Clips.push_back(Entry.path());
    }

    std::sort(Clips.begin(), Clips.end(), [](const auto& A, const auto& B)
    {
        return ToLowerCopy(A.filename().string()) < ToLowerCopy(B.filename().string());
    });

    PortraitClip Clip = {};
    for (const auto& ClipPath : Clips)
    {
        std::optional<std::vector<DecodedVideoFrame>> FramesOpt;
        if (mMediaFoundationInitialized)
            FramesOpt = DecodeVideoFramesFromFile(ClipPath);
        if (!FramesOpt || FramesOpt->empty())
        {
            auto FrameOpt = DecodeFirstVideoFrameWithFfmpeg(ClipPath);
            if (FrameOpt)
                FramesOpt = std::vector<DecodedVideoFrame>{ std::move(*FrameOpt) };
        }
        if (!FramesOpt || FramesOpt->empty())
        {
            Core::Utils::Logger::Warning("ResourceCache",
                "Failed to decode portrait clip: " + ClipPath.string());
            continue;
        }

        for (auto& DecodedFrame : *FramesOpt)
        {
            PortraitFrame Frame = {};
            Frame.Image = UploadRgbaFull(DecodedFrame.Image.Width,
                                         DecodedFrame.Image.Height,
                                         DecodedFrame.Image.Rgba.data());
            Frame.DurationSeconds = DecodedFrame.DurationSeconds;
            if (!Frame.Image.Valid())
                continue;

            Clip.Width = std::max(Clip.Width, DecodedFrame.Image.Width);
            Clip.Height = std::max(Clip.Height, DecodedFrame.Image.Height);
            Clip.Frames.push_back(std::move(Frame));
        }
    }

    if (!Clip.Valid())
    {
        std::vector<std::filesystem::path> ImageFrames;
        for (const auto& Entry : std::filesystem::directory_iterator(PortraitDir))
        {
            if (!Entry.is_regular_file())
                continue;

            const std::string FileName = ToLowerCopy(Entry.path().filename().string());
            const std::string Ext = ToLowerCopy(Entry.path().extension().string());
            if (FileName.find("fid") == std::string::npos)
                continue;
            if (Ext == ".png" || Ext == ".jpg" || Ext == ".jpeg")
                ImageFrames.push_back(Entry.path());
        }

        std::sort(ImageFrames.begin(), ImageFrames.end(), [](const auto& A, const auto& B)
        {
            return ToLowerCopy(A.filename().string()) < ToLowerCopy(B.filename().string());
        });

        for (const auto& ImagePath : ImageFrames)
        {
            auto ImageOpt = DecodeRgbaImageFile(ImagePath);
            if (!ImageOpt || ImageOpt->Rgba.empty())
            {
                Core::Utils::Logger::Warning("ResourceCache",
                    "Failed to decode portrait image: " + ImagePath.string());
                continue;
            }

            PortraitFrame Frame = {};
            Frame.Image = UploadRgbaFull(ImageOpt->Width, ImageOpt->Height, ImageOpt->Rgba.data());
            Frame.DurationSeconds = 1.0f / 12.0f;
            if (!Frame.Image.Valid())
                continue;

            Clip.Width = std::max(Clip.Width, ImageOpt->Width);
            Clip.Height = std::max(Clip.Height, ImageOpt->Height);
            Clip.Frames.push_back(std::move(Frame));
        }
    }

    if (!Clip.Valid())
        return false;

    Core::Utils::Logger::Info("ResourceCache",
        "Loaded portrait clip " + PortraitKey + ": " + std::to_string(Clip.Frames.size()) + " frames");
    OutClip = std::move(Clip);
    return true;
#else
    (void)PortraitKey;
    (void)OutClip;
    return false;
#endif
}
} // namespace Resource::Graphics

