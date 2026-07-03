#include "Resource/RgbaImageFileDecoder.h"

#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"

#include <cstddef>

#ifdef _WIN32
#include <objbase.h>
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#endif

namespace Resource::Graphics
{

std::optional<DecodedRgbaImage> DecodeRgbaImageFile(const std::filesystem::path& Path)
{
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
}

std::optional<DecodedRgbaImage> LoadWicRgbaImageFromFile(const std::filesystem::path& Path)
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
    (void)Path;
    return std::nullopt;
#endif
}
} // namespace Resource::Graphics


