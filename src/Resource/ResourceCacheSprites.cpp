#include "Resource/ResourceCache.h"

#include "Core/Utils/Logger.h"

#include "Resource/DdsGrpDecoder.h"
#include "Resource/GrpToGpu.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
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

const std::vector<GpuImage>* ResourceCache::GetSpriteFrames(uint32_t ImageID)
{
    auto It = mSprites.FramesByImageId.find(ImageID);
    if (It != mSprites.FramesByImageId.end()) return &It->second;

    std::string GrpPath = mData.Loader.ResolveGrpPath(ImageID);
    if (GrpPath.empty()) return nullptr;

    if (GrpPath.size() > 4 && ToLowerCopy(GrpPath.substr(GrpPath.size() - 4)) == ".grp")
    {
        const std::string HdPath = GrpPath.substr(0, GrpPath.size() - 4) + ".dds.grp";
        auto HdData = ReadFile(HdPath);
        if (HdData)
        {
            auto HdFrames = LoadDdsGrpRgbaFrames(*HdData);
            if (HdFrames && !HdFrames->empty())
            {
                auto& Out = mSprites.FramesByImageId[ImageID];
                Out.reserve(HdFrames->size());
                for (const auto& Frame : *HdFrames)
                    Out.push_back(UploadRgbaFull(Frame.Width, Frame.Height, Frame.Rgba.data()));

                Core::Utils::Logger::Info("ResourceCache",
                    "Loaded HD unit sprite: ImageID=" + std::to_string(ImageID) +
                    " -> " + HdPath + " (" + std::to_string(Out.size()) + " frames)");
                return &Out;
            }

            Core::Utils::Logger::Warning("ResourceCache",
                "Failed to decode HD unit sprite: " + HdPath +
                "; falling back to classic GRP");
        }
    }

    std::optional<std::vector<uint8_t>> GrpData;
    if (mSources.IsCasc)
    {
        GrpData = ReadFile(GrpPath);
        if (!GrpData) GrpData = ReadFile("arr/" + GrpPath);
        if (!GrpData) GrpData = ReadFile("arr\\" + GrpPath);
        if (!GrpData) GrpData = ReadFile("data/" + GrpPath);
        if (!GrpData) GrpData = ReadFile("data\\" + GrpPath);
    }
    else
    {
        GrpData = ReadWithFallback(GrpPath);
        if (!GrpData) GrpData = ReadWithFallback("arr/" + GrpPath);
        if (!GrpData) GrpData = ReadWithFallback("data/" + GrpPath);
    }
    if (!GrpData)
    {
        Core::Utils::Logger::Warning("ResourceCache",
            "GRP not found: " + GrpPath + " (ImageID=" + std::to_string(ImageID) + ")");
        return nullptr;
    }

    auto SpriteOpt = DecodeGrpToRgba(*GrpData, mData.ActivePalette);
    if (!SpriteOpt)
    {
        Core::Utils::Logger::Warning("ResourceCache",
            "Failed to decode GRP: " + GrpPath);
        return nullptr;
    }

    auto& Out = mSprites.FramesByImageId[ImageID];
    Out.reserve(SpriteOpt->FrameCount());
    for (auto& F : SpriteOpt->Frames)
    {
        Out.push_back(UploadRgbaFull(F.Width, F.Height, F.Rgba.data()));
    }

    if (ImageID >= 100 && ImageID <= 103)
    {
        Core::Utils::Logger::Info("ResourceCache",
            "Resource GRP: ImageID=" + std::to_string(ImageID) +
            " -> " + GrpPath + " (" + std::to_string(Out.size()) + " frames)");
    }
    return &Out;
}

} // namespace Resource::Graphics
