#include "Render/Draw2D.h"
#include "Render/Private/SokolGpuInterop.h"

#include "Core/Platform/Platform.h"
#include "Render/BitmapFont.h"
#include "Core/Utils/Logger.h"
#include "third_party/sokol/sokol_app.h"

#include <algorithm>
#include <limits>
#include <cstring>
#include <cstdlib>

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

    bool DebugDraw2DEnabled()
    {
        static const bool Enabled = std::getenv("STARCRAFT_DEBUG_DRAW2D") != nullptr;
        return Enabled;
    }
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
        mWhiteTexture.Image = Render::FromSokol(sg_make_image(&Desc));

        sg_view_desc VDesc = {};
        VDesc.texture.image = Render::ToSokol(mWhiteTexture.Image);
        VDesc.label = "white-1x1-view";
        mWhiteTexture.View = Render::FromSokol(sg_make_view(&VDesc));
    }

    // Create dynamic vertex / index buffers.
    // Use one large append-only stream buffer per pass so the same frame can
    // flush many texture/material batches without tripping the backend's
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
            mVbuf.Buffer = Render::FromSokol(sg_make_buffer(&VDesc));
            mIbuf.Buffer = Render::FromSokol(sg_make_buffer(&IDesc));
        }
        else
        {
            mVbufScreen.Buffer = Render::FromSokol(sg_make_buffer(&VDesc));
            mIbufScreen.Buffer = Render::FromSokol(sg_make_buffer(&IDesc));
        }
    }
    mCurrentVbuf = mVbuf;
    mCurrentIbuf = mIbuf;

    bool AnyOk = true;
    if (!mVbuf.Valid()) AnyOk = false;
    if (!mIbuf.Valid()) AnyOk = false;
    if (!mVbufScreen.Valid()) AnyOk = false;
    if (!mIbufScreen.Valid()) AnyOk = false;
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
    mFontTexture = g_Font.GetTexture();
    mFontCharWidth = g_Font.CellW;
    mFontCharHeight = g_Font.CellH;

    mCurrentAlphaBlend = true;
    return true;
}

void Draw2D::Shutdown()
{
    if (mWhiteTexture.View.id) { sg_destroy_view(Render::ToSokol(mWhiteTexture.View)); mWhiteTexture.View = {}; }
    if (mWhiteTexture.Image.id) { sg_destroy_image(Render::ToSokol(mWhiteTexture.Image)); mWhiteTexture.Image = {}; }
    if (mIbuf.Buffer.id) { sg_destroy_buffer(Render::ToSokol(mIbuf.Buffer)); mIbuf.Buffer = {}; }
    if (mVbuf.Buffer.id) { sg_destroy_buffer(Render::ToSokol(mVbuf.Buffer)); mVbuf.Buffer = {}; }
    if (mIbufScreen.Buffer.id) { sg_destroy_buffer(Render::ToSokol(mIbufScreen.Buffer)); mIbufScreen.Buffer = {}; }
    if (mVbufScreen.Buffer.id) { sg_destroy_buffer(Render::ToSokol(mVbufScreen.Buffer)); mVbufScreen.Buffer = {}; }
    mShader.Shutdown();
    if (g_FontInitialized)
    {
        g_Font.Shutdown();
        g_FontInitialized = false;
    }
}

void Draw2D::SetPipeline(bool AlphaBlend, BatchMaterial Material)
{
    if (mCurrentMaterial != Material)
    {
        Flush();
        mCurrentMaterial = Material;
    }
    if (mCurrentAlphaBlend != AlphaBlend)
    {
        Flush();
        mShader.RebuildPipeline(AlphaBlend, false);
        mCurrentAlphaBlend = AlphaBlend;
    }
    if (Material == BatchMaterial::HealthBar)
        mShader.BindHealthBarPipeline();
    else if (Material == BatchMaterial::SpriteEffect)
        mShader.BindSpriteEffectPipeline();
    else
        mShader.BindPipeline();
}

void Draw2D::SetHealthBarParams(float Fraction, Color Fill)
{
    Fraction = std::clamp(Fraction, 0.0f, 1.0f);
    Flush();
    auto ToFloat = [](uint8_t V) { return (float)V / 255.0f; };
    mHealthBarParams.SolidColor[0] = ToFloat(Fill.R);
    mHealthBarParams.SolidColor[1] = ToFloat(Fill.G);
    mHealthBarParams.SolidColor[2] = ToFloat(Fill.B);
    mHealthBarParams.SolidColor[3] = ToFloat(Fill.A);
    mHealthBarParams.MultiplyColor[0] = Fraction;
    mHealthBarParams.MultiplyColor[1] = Fraction;
    mHealthBarParams.MultiplyColor[2] = 0.0f;
    mHealthBarParams.MultiplyColor[3] = 1.0f;
    mHealthBarParams.TeamColor[0] = ToFloat(Fill.R);
    mHealthBarParams.TeamColor[1] = ToFloat(Fill.G);
    mHealthBarParams.TeamColor[2] = ToFloat(Fill.B);
    mHealthBarParams.TeamColor[3] = ToFloat(Fill.A);
}

void Draw2D::SetSpriteEffectParams(float Hallucination,
                                   float Cloaked,
                                   float Brighten,
                                   float Shadow,
                                   Color TeamColor,
                                   bool UseTeamColor)
{
    Flush();
    auto ToFloat = [](uint8_t V) { return (float)V / 255.0f; };
    mSpriteEffectParams.MultiplyColor[0] = 1.0f;
    mSpriteEffectParams.MultiplyColor[1] = 1.0f;
    mSpriteEffectParams.MultiplyColor[2] = 1.0f;
    mSpriteEffectParams.MultiplyColor[3] = 1.0f;
    mSpriteEffectParams.TeamColor[0] = ToFloat(TeamColor.R);
    mSpriteEffectParams.TeamColor[1] = ToFloat(TeamColor.G);
    mSpriteEffectParams.TeamColor[2] = ToFloat(TeamColor.B);
    mSpriteEffectParams.TeamColor[3] = ToFloat(TeamColor.A);
    mSpriteEffectParams.EffectData[0] = std::clamp(Hallucination, 0.0f, 1.0f);
    mSpriteEffectParams.EffectData[1] = std::clamp(Cloaked, 0.0f, 1.0f);
    mSpriteEffectParams.EffectData[2] = std::clamp(Brighten, 0.0f, 1.0f);
    mSpriteEffectParams.EffectData[3] = UseTeamColor ? 1.0f : 0.0f;
    mSpriteEffectParams.EffectMode[0] = std::clamp(Shadow, 0.0f, 1.0f);
    mSpriteEffectParams.EffectMode[1] = 0.0f;
    mSpriteEffectParams.EffectMode[2] = 0.0f;
    mSpriteEffectParams.EffectMode[3] = 0.0f;
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
    mCurrentTexture = mWhiteTexture;
    mCurrentTeamColorMask = {};
    mCurrentMaterial = BatchMaterial::Textured;
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
    mCurrentTexture = mWhiteTexture;
    mCurrentTeamColorMask = {};
    mCurrentMaterial = BatchMaterial::Textured;
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

    if (mVerticesInBatch + 4 > kMaxBatchVerts || mIndicesInBatch + 6 > kMaxBatchIndices)
    {
        Flush();
    }

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
    if (!mCurrentVbuf.Valid() || !mCurrentIbuf.Valid())
    {
        Core::Utils::Logger::Error("Draw2D",
            std::string("Draw2D flush without valid buffers (screen=") +
            std::string(mInScreenPass ? "true" : "false") +
            ", verts=" + std::to_string(mVerticesInBatch) +
            ", idx=" + std::to_string(mIndicesInBatch) + ")");
        mVerticesInBatch = 0;
        mIndicesInBatch = 0;
        return;
    }

    static int FlushCount = 5;
    if (DebugDraw2DEnabled() && FlushCount > 0)
    {
        FlushCount--;
        Core::Utils::Logger::Info("Draw2D",
            "FLUSH: " + std::to_string(mVerticesInBatch) + " verts " +
            std::to_string(mIndicesInBatch) + " idx pass=" +
            std::to_string((int)mInScreenPass));
    }

    sg_range VRange = { mVertices.data(), (size_t)mVerticesInBatch * sizeof(Vertex) };
    sg_range IRange = { mIndices.data(), (size_t)mIndicesInBatch * sizeof(uint16_t) };
    if (sg_query_buffer_will_overflow(Render::ToSokol(mCurrentVbuf.Buffer), VRange.size) ||
        sg_query_buffer_will_overflow(Render::ToSokol(mCurrentIbuf.Buffer), IRange.size))
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

    if (mVerticesInBatch > std::numeric_limits<int>::max() ||
        mIndicesInBatch > std::numeric_limits<int>::max())
    {
        Core::Utils::Logger::Warning("Draw2D", "Draw2D batch exceeds int range, dropping frame batch");
        mVerticesInBatch = 0;
        mIndicesInBatch = 0;
        return;
    }

    const int VertexOffset = sg_append_buffer(Render::ToSokol(mCurrentVbuf.Buffer), &VRange);
    if (VertexOffset < 0)
    {
        Core::Utils::Logger::Warning("Draw2D",
            std::string("Draw2D vertex append failed for ") +
            (mInScreenPass ? "screen" : "world") +
            " pass, verts=" + std::to_string(mVerticesInBatch) +
            ", idx=" + std::to_string(mIndicesInBatch));
        mVerticesInBatch = 0;
        mIndicesInBatch = 0;
        return;
    }

    const int IndexOffset = sg_append_buffer(Render::ToSokol(mCurrentIbuf.Buffer), &IRange);
    if (IndexOffset < 0)
    {
        Core::Utils::Logger::Warning("Draw2D",
            std::string("Draw2D index append failed for ") +
            (mInScreenPass ? "screen" : "world") +
            " pass, verts=" + std::to_string(mVerticesInBatch) +
            ", idx=" + std::to_string(mIndicesInBatch));
        mVerticesInBatch = 0;
        mIndicesInBatch = 0;
        return;
    }

    // Apply uniforms (mat4 mvp)
    sg_apply_uniforms(0, { &mCurrentProjection, sizeof(mCurrentProjection) });
    if (mCurrentMaterial == BatchMaterial::HealthBar)
    {
        mShader.ApplyHealthBarParams(mHealthBarParams);
    }
    else if (mCurrentMaterial == BatchMaterial::SpriteEffect)
    {
        mShader.ApplySpriteEffectParams(mSpriteEffectParams);
    }

    // Bind pipeline + resources
    sg_bindings B = {};
    B.vertex_buffers[0] = Render::ToSokol(mCurrentVbuf.Buffer);
    B.vertex_buffer_offsets[0] = VertexOffset;
    B.index_buffer = Render::ToSokol(mCurrentIbuf.Buffer);
    B.index_buffer_offset = IndexOffset;
    if (mCurrentMaterial == BatchMaterial::Textured ||
        mCurrentMaterial == BatchMaterial::SpriteEffect)
    {
        B.views[0] = Render::ToSokol(mCurrentTexture.View);
        B.samplers[0] = Render::ToSokol(mShader.GetSampler().Sampler);
        if (mCurrentMaterial == BatchMaterial::SpriteEffect)
        {
            B.views[1] = Render::ToSokol(
                mCurrentTeamColorMask.Valid() ? mCurrentTeamColorMask.View : mWhiteTexture.View);
        }
    }
    sg_apply_bindings(&B);

    sg_draw(0, mIndicesInBatch, 1);

    mVerticesInBatch = 0;
    mIndicesInBatch = 0;
}

void Draw2D::EndFrame()
{
    Flush();
}

void Draw2D::DrawSprite(const GpuTextureHandle& Texture,
                        float X, float Y, float W, float H,
                        float U0, float V0, float U1, float V1,
                        Color Tint, bool FlipX)
{
    if (Texture.Image.id == 0) return;
    if (mCurrentTexture.Image.id != Texture.Image.id || mCurrentMaterial != BatchMaterial::Textured)
    {
        Flush();
        mCurrentTexture = Texture;
    }
    SetPipeline(true, BatchMaterial::Textured);
    AddQuad(X, Y, W, H, U0, V0, U1, V1, Tint, FlipX);
    if (mVerticesInBatch + 4 > kMaxBatchVerts) Flush();
}

void Draw2D::DrawSpriteEffect(const GpuTextureHandle& Texture,
                              float X, float Y, float W, float H,
                              float U0, float V0, float U1, float V1,
                              Color Tint,
                              bool FlipX,
                              float Hallucination,
                              float Cloaked,
                              float Brighten,
                              float Shadow,
                              Color TeamColor,
                              const GpuTextureHandle* TeamColorMask)
{
    if (Texture.Image.id == 0) return;
    const bool UseTeamColor = TeamColorMask && TeamColorMask->Valid();
    if (mCurrentTexture.Image.id != Texture.Image.id || mCurrentMaterial != BatchMaterial::SpriteEffect)
    {
        Flush();
        mCurrentTexture = Texture;
    }
    if (UseTeamColor && mCurrentTeamColorMask.Image.id != TeamColorMask->Image.id)
    {
        Flush();
        mCurrentTeamColorMask = *TeamColorMask;
    }
    else if (!UseTeamColor && mCurrentTeamColorMask.Valid())
    {
        Flush();
        mCurrentTeamColorMask = {};
    }
    SetSpriteEffectParams(Hallucination, Cloaked, Brighten, Shadow, TeamColor, UseTeamColor);
    SetPipeline(true, BatchMaterial::SpriteEffect);
    AddQuad(X, Y, W, H, U0, V0, U1, V1, Tint, FlipX);
    Flush();
}

void Draw2D::DrawSpriteFromImage(const GpuTextureHandle& Texture,
                                 float X, float Y, float W, float H, Color Tint)
{
    DrawSprite(Texture, X, Y, W, H, 0.0f, 0.0f, 1.0f, 1.0f, Tint, false);
}

void Draw2D::DrawRect(float X, float Y, float W, float H, Color C)
{
    if (mCurrentTexture.Image.id != mWhiteTexture.Image.id || !mCurrentAlphaBlend)
    {
        Flush();
        mCurrentTexture = mWhiteTexture;
    }
    SetPipeline(true, BatchMaterial::Textured);
    AddQuad(X, Y, W, H, 0.0f, 0.0f, 1.0f, 1.0f, C, false);
    if (mVerticesInBatch + 4 > kMaxBatchVerts) Flush();
}

void Draw2D::DrawHealthBar(float X, float Y, float W, float H, float Fraction, Color Fill)
{
    SetHealthBarParams(Fraction, Fill);
    SetPipeline(true, BatchMaterial::HealthBar);
    AddQuad(X, Y, W, H, 0.0f, 0.0f, 1.0f, 1.0f, Fill, false);
    Flush();
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
    if (mFontTexture.Image.id == 0) return;
    if (mCurrentTexture.Image.id != mFontTexture.Image.id || mCurrentMaterial != BatchMaterial::Textured)
    {
        Flush();
        mCurrentTexture = mFontTexture;
    }
    SetPipeline(true, BatchMaterial::Textured);
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
