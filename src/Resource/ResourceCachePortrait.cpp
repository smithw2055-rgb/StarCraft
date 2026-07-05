#include "Resource/ResourceCache.h"

#include "Core/Utils/Logger.h"

#include "Resource/GrpToGpu.h"
#include "Resource/PortraitVideoDecoder.h"
#include "Resource/RgbaImageFileDecoder.h"

#include <algorithm>
#include <cctype>
#include <cmath>
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

    PortraitFrame UploadPortraitFrame(const DecodedVideoFrame& DecodedFrame)
    {
        PortraitFrame Frame = {};
        Frame.Image = UploadRgbaFull(DecodedFrame.Image.Width,
                                     DecodedFrame.Image.Height,
                                     DecodedFrame.Image.Rgba.data());
        Frame.DurationSeconds = DecodedFrame.DurationSeconds;
        return Frame;
    }

    bool SetStreamingPortraitFrame(PortraitClip& Clip, const DecodedVideoFrame& DecodedFrame)
    {
        PortraitFrame NewFrame = UploadPortraitFrame(DecodedFrame);
        if (!NewFrame.Image.Valid())
            return false;

        DestroyImage(Clip.CurrentFrame.Image);
        Clip.CurrentFrame = std::move(NewFrame);
        Clip.Width = std::max(Clip.Width, DecodedFrame.Image.Width);
        Clip.Height = std::max(Clip.Height, DecodedFrame.Image.Height);
        return true;
    }

    bool OpenStreamingPortraitSegment(PortraitClip& Clip, size_t StartIndex)
    {
        if (Clip.WebmPaths.empty())
            return false;

        for (size_t Attempt = 0; Attempt < Clip.WebmPaths.size(); ++Attempt)
        {
            const size_t Index = (StartIndex + Attempt) % Clip.WebmPaths.size();
            Clip.WebmDecoder.Close();
            if (!Clip.WebmDecoder.Open(Clip.WebmPaths[Index]))
                continue;

            DecodedVideoFrame Frame = {};
            if (!Clip.WebmDecoder.ReadNextFrame(Frame))
                continue;

            if (!SetStreamingPortraitFrame(Clip, Frame))
                continue;

            Clip.ActiveWebmPath = Index;
            return true;
        }

        return false;
    }

    const PortraitFrame* StaticPortraitFrameAt(const PortraitClip& Clip, double TimeSeconds)
    {
        if (Clip.Frames.empty())
            return nullptr;

        double TotalSeconds = 0.0;
        for (const auto& Frame : Clip.Frames)
            TotalSeconds += std::max(0.01f, Frame.DurationSeconds);

        double LocalTime = TotalSeconds > 0.0 ? std::fmod(TimeSeconds, TotalSeconds) : 0.0;
        for (const auto& Frame : Clip.Frames)
        {
            LocalTime -= std::max(0.01f, Frame.DurationSeconds);
            if (LocalTime <= 0.0)
                return &Frame;
        }

        return &Clip.Frames.back();
    }

    const PortraitFrame* StreamingPortraitFrameAt(PortraitClip& Clip, double TimeSeconds)
    {
        if (!Clip.CurrentFrame.Image.Valid())
            return nullptr;

        if (Clip.NextFrameTimeSeconds <= 0.0 || TimeSeconds < Clip.NextFrameTimeSeconds - 1.0)
        {
            Clip.NextFrameTimeSeconds = TimeSeconds + std::max(0.01f, Clip.CurrentFrame.DurationSeconds);
            return &Clip.CurrentFrame;
        }

        int AdvanceBudget = 180;
        while (TimeSeconds >= Clip.NextFrameTimeSeconds && AdvanceBudget-- > 0)
        {
            DecodedVideoFrame Frame = {};
            if (!Clip.WebmDecoder.ReadNextFrame(Frame))
            {
                const size_t NextSegment = Clip.WebmPaths.empty() ? 0 : (Clip.ActiveWebmPath + 1) % Clip.WebmPaths.size();
                if (!OpenStreamingPortraitSegment(Clip, NextSegment))
                    break;
            }
            else
            {
                SetStreamingPortraitFrame(Clip, Frame);
            }

            Clip.NextFrameTimeSeconds += std::max(0.01f, Clip.CurrentFrame.DurationSeconds);
        }

        if (AdvanceBudget <= 0)
            Clip.NextFrameTimeSeconds = TimeSeconds + std::max(0.01f, Clip.CurrentFrame.DurationSeconds);

        return Clip.CurrentFrame.Image.Valid() ? &Clip.CurrentFrame : nullptr;
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

const PortraitFrame* ResourceCache::GetPortraitFrame(const std::string& PortraitKey, double TimeSeconds)
{
    if (PortraitKey.empty())
        return nullptr;

    const std::string Key = ToLowerCopy(PortraitKey);
    auto It = mUI.PortraitClips.find(Key);
    if (It == mUI.PortraitClips.end())
    {
        PortraitClip Clip = {};
        if (!LoadPortraitClip(Key, Clip))
        {
            mUI.PortraitClips.emplace(Key, PortraitClip{});
            return nullptr;
        }

        It = mUI.PortraitClips.emplace(Key, std::move(Clip)).first;
    }

    PortraitClip& Clip = It->second;
    if (!Clip.Valid())
        return nullptr;

    if (Clip.StreamingWebm)
        return StreamingPortraitFrameAt(Clip, TimeSeconds);
    return StaticPortraitFrameAt(Clip, TimeSeconds);
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
    if (!Clips.empty())
    {
        Clip.StreamingWebm = true;
        Clip.WebmPaths = Clips;
        if (OpenStreamingPortraitSegment(Clip, 0))
        {
            size_t EncodedFrames = Clip.WebmDecoder.EncodedFrameCount();
            for (size_t I = 1; I < Clip.WebmPaths.size(); ++I)
            {
                WebmVideoDecoder Probe;
                if (Probe.Open(Clip.WebmPaths[I]))
                    EncodedFrames += Probe.EncodedFrameCount();
            }

            Core::Utils::Logger::Info("ResourceCache",
                "Loaded portrait clip " + PortraitKey + ": streaming " +
                std::to_string(Clip.WebmPaths.size()) + " webm files, " +
                std::to_string(EncodedFrames) + " encoded frames");
            OutClip = std::move(Clip);
            return true;
        }

        Core::Utils::Logger::Warning("ResourceCache",
            "Failed to open streaming portrait webm clip: " + PortraitKey);
        DestroyImage(Clip.CurrentFrame.Image);
        Clip = {};
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

