#include "Resource/PortraitVideoDecoder.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace
{
    struct CoverageStats
    {
        size_t Files = 0;
        size_t Decoded = 0;
        size_t WithAlphaVariation = 0;
        size_t XamlTags = 0;
    };

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

    bool ShouldCoverWebm(const std::filesystem::path& Path)
    {
        std::string Text = Path.generic_string();
        for (char& Ch : Text)
            if (Ch >= 'A' && Ch <= 'Z')
                Ch = char(Ch - 'A' + 'a');

        return Text.rfind("assets/portrait/", 0) == 0 ||
               Text.rfind("assets/glue/", 0) == 0 ||
               Text.find("/assets/sd/glue/") != std::string::npos ||
               Text.find("/assets/sd/smk/") != std::string::npos ||
               Text.rfind("assets/webui/dist/lib/images/", 0) == 0;
    }

    int Fail(const std::string& Message)
    {
        std::cerr << Message << "\n";
        return 1;
    }

    bool DecodeFirstFrame(const std::filesystem::path& Path, bool& OutHasAlpha)
    {
        Resource::Graphics::WebmVideoDecoder Decoder;
        if (!Decoder.Open(Path))
            return false;

        Resource::Graphics::DecodedVideoFrame Frame;
        if (!Decoder.ReadNextFrame(Frame))
            return false;

        if (Frame.Image.Width <= 0 ||
            Frame.Image.Height <= 0 ||
            Frame.Image.Rgba.size() != (size_t)Frame.Image.Width * (size_t)Frame.Image.Height * 4u)
        {
            return false;
        }

        OutHasAlpha = HasAlphaVariation(Frame);
        return true;
    }

    std::vector<std::filesystem::path> ExtractGlueXamlWebmTags(const std::filesystem::path& XamlPath)
    {
        std::ifstream File(XamlPath);
        if (!File)
            return {};

        std::string Text((std::istreambuf_iterator<char>(File)),
                         std::istreambuf_iterator<char>());
        std::set<std::filesystem::path> Paths;
        size_t Pos = 0;
        while ((Pos = Text.find("webm:", Pos)) != std::string::npos)
        {
            Pos += 5;
            size_t End = Pos;
            while (End < Text.size() &&
                   Text[End] != '"' &&
                   Text[End] != '\'' &&
                   Text[End] != '|' &&
                   Text[End] != '<' &&
                   Text[End] != '>')
            {
                ++End;
            }

            if (End > Pos)
                Paths.insert(std::filesystem::path("assets") / Text.substr(Pos, End - Pos));
            Pos = End;
        }

        return std::vector<std::filesystem::path>(Paths.begin(), Paths.end());
    }
}

int main()
{
    const std::filesystem::path Root = "assets";
    if (!std::filesystem::exists(Root))
        return Fail("assets directory is missing");

    CoverageStats Stats;
    std::vector<std::string> Failures;
    for (const auto& Entry : std::filesystem::recursive_directory_iterator(Root))
    {
        if (!Entry.is_regular_file() || Entry.path().extension() != ".webm")
            continue;

        const std::filesystem::path Path = Entry.path();
        if (!ShouldCoverWebm(Path))
            continue;

        ++Stats.Files;
        bool HasAlpha = false;
        if (!DecodeFirstFrame(Path, HasAlpha))
        {
            Failures.push_back("decode failed: " + Path.generic_string());
            continue;
        }

        ++Stats.Decoded;
        if (HasAlpha)
            ++Stats.WithAlphaVariation;
    }

    const auto XamlWebms = ExtractGlueXamlWebmTags("assets/ui/glue/GlueMenu.xaml");
    for (const auto& Path : XamlWebms)
    {
        ++Stats.XamlTags;
        bool HasAlpha = false;
        if (!DecodeFirstFrame(Path, HasAlpha))
            Failures.push_back("xaml webm decode failed: " + Path.generic_string());
    }

    if (Stats.Files == 0)
        return Fail("no covered webm assets were found");
    if (Stats.XamlTags == 0)
        return Fail("no glue xaml webm tags were found");

    if (!Failures.empty())
    {
        for (const auto& Failure : Failures)
            std::cerr << Failure << "\n";
        return Fail("webm asset coverage failed");
    }

    if (Stats.WithAlphaVariation == 0)
        return Fail("covered webm assets decoded without alpha variation");

    std::cout << "covered webm files=" << Stats.Files
              << " decoded first frames=" << Stats.Decoded
              << " alpha first frames=" << Stats.WithAlphaVariation
              << " xaml webm tags=" << Stats.XamlTags << "\n";
    return 0;
}
