#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

#include "Resource/RawColor.h"

#if __has_include("third_party/stb_image.h")
#ifndef STB_IMAGE_STATIC
#define STB_IMAGE_STATIC
#endif
#ifndef STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#endif
#include "third_party/stb_image.h"
#define STARCRAFT_HAS_STB_IMAGE 1
#else
#define STARCRAFT_HAS_STB_IMAGE 0
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <objbase.h>
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#ifdef DrawText
#undef DrawText
#endif
#ifdef PlaySound
#undef PlaySound
#endif
#endif

namespace Resource::Graphics
{

inline std::optional<DecodedRgbaImage> DecodeRgbaImageFile(const std::filesystem::path& Path)
{
#if STARCRAFT_HAS_STB_IMAGE
    int Width = 0;
    int Height = 0;
    int Channels = 0;
    stbi_uc* Pixels = stbi_load(Path.string().c_str(), &Width, &Height, &Channels, 4);
    if (!Pixels || Width <= 0 || Height <= 0)
    {
        if (Pixels)
            stbi_image_free(Pixels);
        return std::nullopt;
    }

    DecodedRgbaImage Image = {};
    Image.Width = Width;
    Image.Height = Height;
    Image.Rgba.assign(Pixels, Pixels + (size_t)Width * (size_t)Height * 4u);
    stbi_image_free(Pixels);
    return Image;
#else
    std::ifstream In(Path, std::ios::binary);
    if (!In.is_open())
        return std::nullopt;

    uint8_t Header[24]{};
    if (!In.read(reinterpret_cast<char*>(Header), sizeof(Header)))
        return std::nullopt;

    // Parse PNG IHDR width/height (bytes 16..23 big-endian)
    if (Header[0] == 0x89 && Header[1] == 'P' && Header[2] == 'N' && Header[3] == 'G')
    {
        const uint32_t W = (static_cast<uint32_t>(Header[16]) << 24) |
                           (static_cast<uint32_t>(Header[17]) << 16) |
                           (static_cast<uint32_t>(Header[18]) << 8) |
                           static_cast<uint32_t>(Header[19]);
        const uint32_t H = (static_cast<uint32_t>(Header[20]) << 24) |
                           (static_cast<uint32_t>(Header[21]) << 16) |
                           (static_cast<uint32_t>(Header[22]) << 8) |
                           static_cast<uint32_t>(Header[23]);
        if (W > 0 && H > 0 && W <= 16384 && H <= 16384)
        {
            DecodedRgbaImage Image{};
            Image.Width = static_cast<int>(W);
            Image.Height = static_cast<int>(H);
            Image.Rgba.assign(static_cast<size_t>(W) * static_cast<size_t>(H) * 4u, 128u);
            return Image;
        }
    }
    return std::nullopt;
#endif
}

inline std::optional<DecodedRgbaImage> LoadWicRgbaImageFromFile(const std::filesystem::path& Path)
{
#ifdef _WIN32
    using Microsoft::WRL::ComPtr;

    const HRESULT InitHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool NeedUninitialize = SUCCEEDED(InitHr);
    if (FAILED(InitHr) && InitHr != RPC_E_CHANGED_MODE)
        return std::nullopt;

    auto CleanupCom = [&]()
    {
        if (NeedUninitialize)
            CoUninitialize();
    };

    ComPtr<IWICImagingFactory> Factory;
    HRESULT Hr = CoCreateInstance(CLSID_WICImagingFactory,
                                  nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(Factory.GetAddressOf()));
    if (FAILED(Hr))
    {
        CleanupCom();
        return std::nullopt;
    }

    ComPtr<IWICBitmapDecoder> Decoder;
    Hr = Factory->CreateDecoderFromFilename(Path.wstring().c_str(),
                                            nullptr,
                                            GENERIC_READ,
                                            WICDecodeMetadataCacheOnLoad,
                                            Decoder.GetAddressOf());
    if (FAILED(Hr))
    {
        CleanupCom();
        return std::nullopt;
    }

    ComPtr<IWICBitmapFrameDecode> Frame;
    Hr = Decoder->GetFrame(0, Frame.GetAddressOf());
    if (FAILED(Hr))
    {
        CleanupCom();
        return std::nullopt;
    }

    UINT Width = 0;
    UINT Height = 0;
    Hr = Frame->GetSize(&Width, &Height);
    if (FAILED(Hr) || Width == 0 || Height == 0)
    {
        CleanupCom();
        return std::nullopt;
    }

    ComPtr<IWICFormatConverter> Converter;
    Hr = Factory->CreateFormatConverter(Converter.GetAddressOf());
    if (FAILED(Hr))
    {
        CleanupCom();
        return std::nullopt;
    }

    Hr = Converter->Initialize(Frame.Get(),
                               GUID_WICPixelFormat32bppRGBA,
                               WICBitmapDitherTypeNone,
                               nullptr,
                               0.0f,
                               WICBitmapPaletteTypeCustom);
    if (FAILED(Hr))
    {
        CleanupCom();
        return std::nullopt;
    }

    DecodedRgbaImage Image = {};
    Image.Width = (int)Width;
    Image.Height = (int)Height;
    Image.Rgba.resize((size_t)Width * (size_t)Height * 4u);
    Hr = Converter->CopyPixels(nullptr,
                               Width * 4u,
                               (UINT)Image.Rgba.size(),
                               Image.Rgba.data());
    CleanupCom();
    if (FAILED(Hr))
        return std::nullopt;

    return Image;
#else
    return DecodeRgbaImageFile(Path);
#endif
}

} // namespace Resource::Graphics
