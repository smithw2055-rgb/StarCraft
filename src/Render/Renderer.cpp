#include "Render/Renderer.h"

#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>
#endif

#include "third_party/sokol/sokol_app.h"
#include "third_party/sokol/sokol_gfx.h"
#include "third_party/sokol/sokol_glue.h"
#include "third_party/sokol/sokol_log.h"

namespace Render
{


static bool g_active = false;
static sg_pass_action g_passAction = {};

static const char* BackendName(sg_backend backend)
{
    switch (backend)
    {
    case SG_BACKEND_GLCORE: return "GLCORE";
    case SG_BACKEND_GLES3: return "GLES3";
    case SG_BACKEND_D3D11: return "D3D11";
    case SG_BACKEND_METAL_IOS: return "METAL_IOS";
    case SG_BACKEND_METAL_MACOS: return "METAL_MACOS";
    case SG_BACKEND_METAL_SIMULATOR: return "METAL_SIMULATOR";
    case SG_BACKEND_WGPU: return "WGPU";
    case SG_BACKEND_DUMMY: return "DUMMY";
    default: return "UNKNOWN";
    }
}

bool Initialize()
{
    if (g_active) return true;
    sg_desc SetupDesc = {};
    SetupDesc.environment = sglue_environment();
    SetupDesc.logger.func = slog_func;
    SetupDesc.pipeline_pool_size = 256;
    SetupDesc.shader_pool_size = 64;
    SetupDesc.image_pool_size = 32768;
    SetupDesc.buffer_pool_size = 8192;
    SetupDesc.view_pool_size = 32768;
    SetupDesc.sampler_pool_size = 1024;
    SetupDesc.disable_validation = true;
    sg_setup(&SetupDesc);
    if (!sg_isvalid())
    {
        std::cerr << "Renderer: sg_setup failed!" << std::endl;
        return false;
    }
    g_passAction = {};
    g_passAction.colors[0].load_action = SG_LOADACTION_CLEAR;
    g_passAction.colors[0].clear_value.r = 0.08f;
    g_passAction.colors[0].clear_value.g = 0.08f;
    g_passAction.colors[0].clear_value.b = 0.10f;
    g_passAction.colors[0].clear_value.a = 1.0f;
    g_passAction.depth.load_action = SG_LOADACTION_CLEAR;
    g_passAction.depth.clear_value = 1.0f;
    g_active = true;
    std::cout << "Renderer: graphics backend initialized (backend=" << BackendName(sg_query_backend())
              << ", id=" << static_cast<int>(sg_query_backend()) << ")" << std::endl;
    return true;
}

void Shutdown()
{
    if (!g_active) return;
    sg_shutdown();
    g_active = false;
}

bool IsActive() { return g_active; }

void BeginFrame()
{
    sg_pass Pass = {};
    Pass.action = g_passAction;
    Pass.swapchain = sglue_swapchain();
    sg_begin_pass(&Pass);
}

void EndPass()
{
    sg_end_pass();
}

void PrepareNativeOverlay()
{
    sg_pass pass = {};
    pass.swapchain = sglue_swapchain();
    pass.action.colors[0].load_action = SG_LOADACTION_LOAD;
    pass.action.depth.load_action = SG_LOADACTION_LOAD;
    pass.action.stencil.load_action = SG_LOADACTION_LOAD;
    sg_begin_pass(&pass);
    sg_end_pass();
}

void Commit()
{
    sg_commit();
}

void EndFrame()
{
    EndPass();
    Commit();
}

void ResetStateCache()
{
    if (g_active)
        sg_reset_state_cache();
}


void SetClearColor(float R, float G, float B, float A)
{
    g_passAction.colors[0].clear_value.r = R;
    g_passAction.colors[0].clear_value.g = G;
    g_passAction.colors[0].clear_value.b = B;
    g_passAction.colors[0].clear_value.a = A;
}

#if defined(_WIN32)
bool CaptureScreenshot(const std::string& OutputPath)
{
    using Microsoft::WRL::ComPtr;

    sg_environment env = sglue_environment();
    sg_swapchain sc = sglue_swapchain();
    ID3D11Device* device = (ID3D11Device*)env.d3d11.device;
    ID3D11DeviceContext* context = (ID3D11DeviceContext*)env.d3d11.device_context;
    ID3D11RenderTargetView* rtv = (ID3D11RenderTargetView*)sc.d3d11.render_view;

    if (!device || !context || !rtv)
        return false;

    ComPtr<ID3D11Resource> res;
    rtv->GetResource(res.GetAddressOf());
    if (!res)
        return false;

    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = res.As(&backBuffer);
    if (FAILED(hr) || !backBuffer)
        return false;

    D3D11_TEXTURE2D_DESC desc;
    backBuffer->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> staging;
    hr = device->CreateTexture2D(&stagingDesc, nullptr, staging.GetAddressOf());
    if (FAILED(hr) || !staging)
        return false;

    context->CopyResource(staging.Get(), backBuffer.Get());

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr) || !mapped.pData)
        return false;

    int width = static_cast<int>(desc.Width);
    int height = static_cast<int>(desc.Height);
    std::vector<uint8_t> bgra(width * height * 4);

    const uint8_t* srcRow = static_cast<const uint8_t*>(mapped.pData);
    for (int y = 0; y < height; ++y)
    {
        std::memcpy(&bgra[y * width * 4], srcRow + y * mapped.RowPitch, width * 4);
    }
    context->Unmap(staging.Get(), 0);

    const HRESULT initHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool needUninit = SUCCEEDED(initHr);

    ComPtr<IWICImagingFactory> factory;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr))
    {
        if (needUninit) CoUninitialize();
        return false;
    }

    std::filesystem::path outPath(OutputPath);
    if (outPath.has_parent_path())
        std::filesystem::create_directories(outPath.parent_path());

    ComPtr<IWICStream> stream;
    hr = factory->CreateStream(stream.GetAddressOf());
    if (FAILED(hr))
    {
        if (needUninit) CoUninitialize();
        return false;
    }

    hr = stream->InitializeFromFilename(outPath.wstring().c_str(), GENERIC_WRITE);
    if (FAILED(hr))
    {
        if (needUninit) CoUninitialize();
        return false;
    }

    ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf());
    if (FAILED(hr))
    {
        if (needUninit) CoUninitialize();
        return false;
    }

    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr))
    {
        if (needUninit) CoUninitialize();
        return false;
    }

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    hr = encoder->CreateNewFrame(frame.GetAddressOf(), props.GetAddressOf());
    if (FAILED(hr))
    {
        if (needUninit) CoUninitialize();
        return false;
    }

    hr = frame->Initialize(props.Get());
    if (FAILED(hr))
    {
        if (needUninit) CoUninitialize();
        return false;
    }

    hr = frame->SetSize(width, height);
    if (FAILED(hr))
    {
        if (needUninit) CoUninitialize();
        return false;
    }

    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&pixelFormat);
    if (FAILED(hr))
    {
        if (needUninit) CoUninitialize();
        return false;
    }

    hr = frame->WritePixels(height, width * 4, static_cast<UINT>(bgra.size()), bgra.data());
    if (FAILED(hr))
    {
        if (needUninit) CoUninitialize();
        return false;
    }

    hr = frame->Commit();
    if (SUCCEEDED(hr))
        hr = encoder->Commit();

    if (needUninit)
        CoUninitialize();

    return SUCCEEDED(hr);
}
#else
bool CaptureScreenshot(const std::string&) { return false; }
#endif

} // namespace Render

