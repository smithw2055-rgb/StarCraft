#include "Render/sokol/Draw2D.h"

#include "Core/Platform/Platform.h"
#include "Render/sokol/BitmapFont.h"
#include "Core/Utils/Logger.h"
#include "third_party/sokol/sokol_app.h"

#include <cstring>

namespace Render
{

Draw2D g_Draw2D;

namespace
{
    BitmapFont g_Font;
    bool g_FontInitialized = false;
    const int kMaxBatchVerts = 8192;
    const int kMaxBatchIndices = 16384;
    const int kFrameBufferVerts = 131072;
    const int kFrameBufferIndices = 262144;
}

bool Draw2D::Init()
{
    if (!mShader.Init()) return false;

    // Create white 1x1 texture
    {
        uint8_t White[4] = { 255, 255, 255, 255 };
        sg_image_desc Desc = {};
        Desc.width = 1;
        Desc.height = 1;
        Desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        Desc.data.mip_levels[0] = { White, 4 };
        Desc.label = "white-1x1";
        mWhiteImage = sg_make_image(&Desc);

        sg_view_desc VDesc = {};
        VDesc.texture.image = mWhiteImage;
        VDesc.label = "white-1x1-view";
        mWhiteView = sg_make_view(&VDesc);
    }

    // Create dynamic vertex / index buffers.
    // Use one large append-only stream buffer per pass so the same frame can
    // flush many texture/material batches without tripping sokol's
    // one-sg_update_buffer-per-buffer-per-frame rule.
    for (int P = 0; P < 2; P++)
    {
        sg_buffer_desc VDesc = {};
        VDesc.size = kFrameBufferVerts * sizeof(Vertex);
        VDesc.usage.vertex_buffer = true;
        VDesc.usage.stream_update = true;
        VDesc.usage.immutable = false;
        VDesc.label = (P == 0) ? "draw2d-vbuf" : "draw2d-vbuf-screen";

        sg_buffer_desc IDesc = {};
        IDesc.size = kFrameBufferIndices * sizeof(uint16_t);
        IDesc.usage.index_buffer = true;
        IDesc.usage.stream_update = true;
        IDesc.usage.immutable = false;
        IDesc.label = (P == 0) ? "draw2d-ibuf" : "draw2d-ibuf-screen";

        if (P == 0)
        {
            mVbuf = sg_make_buffer(&VDesc);
            mIbuf = sg_make_buffer(&IDesc);
        }
        else
        {
            mVbufScreen = sg_make_buffer(&VDesc);
            mIbufScreen = sg_make_buffer(&IDesc);
        }
    }
    mCurrentVbuf = mVbuf;
    mCurrentIbuf = mIbuf;

    bool AnyOk = true;
    if (mVbuf.id == 0) AnyOk = false;
    if (mIbuf.id == 0) AnyOk = false;
    if (mVbufScreen.id == 0) AnyOk = false;
    if (mIbufScreen.id == 0) AnyOk = false;
    if (!AnyOk)
    {
        Core::Utils::Logger::Error("Draw2D", "Failed to create dynamic buffers");
        return false;
    }

    // Init font
    if (!g_FontInitialized)
    {
        if (!g_Font.Init())
        {
            Core::Utils::Logger::Warning("Draw2D", "Font init failed (text will be skipped)");
        }
        g_FontInitialized = true;
    }
    mFontImage = g_Font.GetImage();
    mFontView = g_Font.GetView();
    mFontCharWidth = g_Font.CellW;
    mFontCharHeight = g_Font.CellH;

    mCurrentAlphaBlend = true;
    return true;
}

void Draw2D::Shutdown()
{
    if (mWhiteView.id) { sg_destroy_view(mWhiteView); mWhiteView = sg_view{0}; }
    if (mWhiteImage.id) { sg_destroy_image(mWhiteImage); mWhiteImage = sg_image{0}; }
    if (mIbuf.id) { sg_destroy_buffer(mIbuf); mIbuf = sg_buffer{0}; }
    if (mVbuf.id) { sg_destroy_buffer(mVbuf); mVbuf = sg_buffer{0}; }
    if (mIbufScreen.id) { sg_destroy_buffer(mIbufScreen); mIbufScreen = sg_buffer{0}; }
    if (mVbufScreen.id) { sg_destroy_buffer(mVbufScreen); mVbufScreen = sg_buffer{0}; }
    mShader.Shutdown();
    if (g_FontInitialized)
    {
        g_Font.Shutdown();
        g_FontInitialized = false;
    }
}

void Draw2D::SetPipeline(bool AlphaBlend)
{
    if (mCurrentAlphaBlend != AlphaBlend)
    {
        mShader.RebuildPipeline(AlphaBlend, false);
        mCurrentAlphaBlend = AlphaBlend;
    }
    mShader.BindPipeline();
}

void Draw2D::EnsureCapacity(int V, int I)
{
    if ((int)mVertices.size() < V) mVertices.resize(V);
    if ((int)mIndices.size() < I) mIndices.resize(I);
}

void Draw2D::BeginFrame(const Camera2D& Camera)
{
    mVertices.clear();
    mIndices.clear();
    mCurrentImage = mWhiteImage;
    mCurrentView = mWhiteView;
    mVerticesInBatch = 0;
    mIndicesInBatch = 0;
    mFrameWidth = Camera.GetViewportWidth();
    mFrameHeight = Camera.GetViewportHeight();
    mInScreenPass = false;
    mCurrentVbuf = mVbuf;
    mCurrentIbuf = mIbuf;

    mCurrentProjection = Camera.GetProjectionMatrix();
}

void Draw2D::BeginFrameScreen()
{
    mVertices.clear();
    mIndices.clear();
    mCurrentImage = mWhiteImage;
    mCurrentView = mWhiteView;
    mVerticesInBatch = 0;
    mIndicesInBatch = 0;
    mFrameWidth = Core::Platform::GetWindowWidth();
    mFrameHeight = Core::Platform::GetWindowHeight();
    mInScreenPass = true;
    mCurrentVbuf = mVbufScreen;
    mCurrentIbuf = mIbufScreen;

    mCurrentProjection = HMM_Orthographic_RH_NO(
        0.0f, (float)mFrameWidth, (float)mFrameHeight, 0.0f, -1000.0f, 1000.0f);
}

void Draw2D::AddQuad(float X, float Y, float W, float H,
                    float U0, float V0, float U1, float V1,
                    Color C, bool FlipX)
{
    if (FlipX) { float T = U0; U0 = U1; U1 = T; }

    EnsureCapacity(mVerticesInBatch + 4, mIndicesInBatch + 6);

    uint32_t Packed = (uint32_t)C.R | ((uint32_t)C.G << 8) | ((uint32_t)C.B << 16) | ((uint32_t)C.A << 24);
    int Base = mVerticesInBatch;
    mVertices[mVerticesInBatch + 0] = { X,     Y,     U0, V0, Packed };
    mVertices[mVerticesInBatch + 1] = { X + W, Y,     U1, V0, Packed };
    mVertices[mVerticesInBatch + 2] = { X + W, Y + H, U1, V1, Packed };
    mVertices[mVerticesInBatch + 3] = { X,     Y + H, U0, V1, Packed };
    mVerticesInBatch += 4;

    mIndices[mIndicesInBatch + 0] = (uint16_t)(Base + 0);
    mIndices[mIndicesInBatch + 1] = (uint16_t)(Base + 1);
    mIndices[mIndicesInBatch + 2] = (uint16_t)(Base + 2);
    mIndices[mIndicesInBatch + 3] = (uint16_t)(Base + 0);
    mIndices[mIndicesInBatch + 4] = (uint16_t)(Base + 2);
    mIndices[mIndicesInBatch + 5] = (uint16_t)(Base + 3);
    mIndicesInBatch += 6;
}

void Draw2D::Flush()
{
    if (mVerticesInBatch == 0) return;

    static int FlushCount = 5;
    if (FlushCount > 0)
    {
        FlushCount--;
        Core::Utils::Logger::Info("Draw2D",
            "FLUSH: " + std::to_string(mVerticesInBatch) + " verts " +
            std::to_string(mIndicesInBatch) + " idx pass=" +
            std::to_string((int)mInScreenPass));
    }

    sg_range VRange = { mVertices.data(), (size_t)mVerticesInBatch * sizeof(Vertex) };
    sg_range IRange = { mIndices.data(), (size_t)mIndicesInBatch * sizeof(uint16_t) };
    if (sg_query_buffer_will_overflow(mCurrentVbuf, VRange.size) ||
        sg_query_buffer_will_overflow(mCurrentIbuf, IRange.size))
    {
        Core::Utils::Logger::Warning("Draw2D",
            std::string("Frame stream buffer overflow in ") +
            (mInScreenPass ? "screen" : "world") +
            " pass, dropping batch: verts=" + std::to_string(mVerticesInBatch) +
            " idx=" + std::to_string(mIndicesInBatch));
        mVerticesInBatch = 0;
        mIndicesInBatch = 0;
        return;
    }

    const int VertexOffset = sg_append_buffer(mCurrentVbuf, &VRange);
    const int IndexOffset = sg_append_buffer(mCurrentIbuf, &IRange);

    // Apply uniforms (mat4 mvp)
    sg_apply_uniforms(0, { &mCurrentProjection, sizeof(mCurrentProjection) });

    // Bind pipeline + resources
    sg_bindings B = {};
    B.vertex_buffers[0] = mCurrentVbuf;
    B.vertex_buffer_offsets[0] = VertexOffset;
    B.index_buffer = mCurrentIbuf;
    B.index_buffer_offset = IndexOffset;
    B.views[0] = mCurrentView;
    B.samplers[0] = mShader.Sampler;
    sg_apply_bindings(&B);

    sg_draw(0, mIndicesInBatch, 1);

    mVerticesInBatch = 0;
    mIndicesInBatch = 0;
}

void Draw2D::EndFrame()
{
    Flush();
}

void Draw2D::DrawSprite(sg_image Image, sg_view View,
                        float X, float Y, float W, float H,
                        float U0, float V0, float U1, float V1,
                        Color Tint, bool FlipX)
{
    if (Image.id == 0) return;
    if (mCurrentImage.id != Image.id)
    {
        Flush();
        mCurrentImage = Image;
        mCurrentView = View;
    }
    SetPipeline(true);
    AddQuad(X, Y, W, H, U0, V0, U1, V1, Tint, FlipX);
    if (mVerticesInBatch + 4 > kMaxBatchVerts) Flush();
}

void Draw2D::DrawSpriteFromImage(const Resource::SokolBridge::SokolImage& Img,
                                 float X, float Y, float W, float H, Color Tint)
{
    DrawSprite(Img.Image, Img.View, X, Y, W, H, 0.0f, 0.0f, 1.0f, 1.0f, Tint, false);
}

void Draw2D::DrawRect(float X, float Y, float W, float H, Color C)
{
    if (mCurrentImage.id != mWhiteImage.id || !mCurrentAlphaBlend)
    {
        Flush();
        mCurrentImage = mWhiteImage;
        mCurrentView = mWhiteView;
    }
    SetPipeline(true);
    AddQuad(X, Y, W, H, 0.0f, 0.0f, 1.0f, 1.0f, C, false);
    if (mVerticesInBatch + 4 > kMaxBatchVerts) Flush();
}

void Draw2D::DrawRectLines(float X, float Y, float W, float H, Color C, float Thickness)
{
    DrawRect(X, Y, W, Thickness, C);
    DrawRect(X, Y + H - Thickness, W, Thickness, C);
    DrawRect(X, Y, Thickness, H, C);
    DrawRect(X + W - Thickness, Y, Thickness, H, C);
}

void Draw2D::DrawText(const std::string& Text, float X, float Y, float Size, Color C)
{
    if (mFontImage.id == 0) return;
    if (mCurrentImage.id != mFontImage.id)
    {
        Flush();
        mCurrentImage = mFontImage;
        mCurrentView = mFontView;
    }
    SetPipeline(true);
    float Cur = X;
    for (char Ch : Text)
    {
        if (Ch == '\n')
        {
            Y += Size;
            Cur = X;
            continue;
        }
        float UV[4];
        g_Font.GetCharUV(Ch, UV);
        float CW = Size * (float)mFontCharWidth / (float)mFontCharHeight;
        AddQuad(Cur, Y, CW, Size, UV[0], UV[1], UV[2], UV[3], C, false);
        Cur += CW;
        if (mVerticesInBatch + 4 > kMaxBatchVerts) { Flush(); }
    }
}

} // namespace Render
