#include "Resource/PortraitVideoDecoder.h"
#include <iostream>
#include <filesystem>
#include <fstream>

int main()
{
    std::filesystem::path Path = "assets/portrait/tadvisor/tadfid00.webm";
    std::cout << "Inspecting " << Path << std::endl;

    Resource::Graphics::WebmVideoDecoder Decoder;
    if (!Decoder.Open(Path))
    {
        std::cerr << "Failed to open " << Path << std::endl;
        return 1;
    }

    std::cout << "Width: " << Decoder.Width() << ", Height: " << Decoder.Height()
              << ", Frames: " << Decoder.EncodedFrameCount() << std::endl;

    Resource::Graphics::DecodedVideoFrame Frame;
    if (!Decoder.ReadNextFrame(Frame))
    {
        std::cerr << "Failed to read first frame" << std::endl;
        return 1;
    }

    std::cout << "First frame: " << Frame.Image.Width << "x" << Frame.Image.Height
              << ", RGBA size: " << Frame.Image.Rgba.size() << std::endl;

    // Check corners and center pixels
    int W = Frame.Image.Width;
    int H = Frame.Image.Height;
    auto PrintPixel = [&](const char* Name, int X, int Y)
    {
        int Idx = (Y * W + X) * 4;
        int R = Frame.Image.Rgba[Idx + 0];
        int G = Frame.Image.Rgba[Idx + 1];
        int B = Frame.Image.Rgba[Idx + 2];
        int A = Frame.Image.Rgba[Idx + 3];
        std::cout << Name << "(" << X << "," << Y << "): R=" << R << " G=" << G << " B=" << B << " A=" << A << "\n";
    };

    PrintPixel("Top-Left", 5, 5);
    PrintPixel("Top-Right", W - 5, 5);
    PrintPixel("Bottom-Left", 5, H - 5);
    PrintPixel("Bottom-Right", W - 5, H - 5);
    PrintPixel("Center", W / 2, H / 2);

    int PureGreenCount = 0;
    int HasAlphaCount = 0;
    for (size_t I = 0; I < Frame.Image.Rgba.size(); I += 4)
    {
        int R = Frame.Image.Rgba[I + 0];
        int G = Frame.Image.Rgba[I + 1];
        int B = Frame.Image.Rgba[I + 2];
        int A = Frame.Image.Rgba[I + 3];
        if (A < 255) HasAlphaCount++;
        if (G > 120 && R < 60 && B < 60) PureGreenCount++;
    }

    std::cout << "Total pixels: " << (W * H)
              << ", Pixels with A < 255: " << HasAlphaCount
              << ", Pixels with Green Dominant: " << PureGreenCount << std::endl;

    return 0;
}
