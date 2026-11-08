#include "Render/Draw2D.h"
#include "Render/GpuTextureHandle.h"

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

void UploadPendingFontGlyphs()
{
    if (g_FontInitialized)
        g_Font.UploadPendingGlyphs();
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
        mCurrentAlphaBlend = AlphaBlend;
    }
    if (!mCurrentVbuf.Valid() || !mCurrentIbuf.Valid())
        return;

    if (Material == BatchMaterial::HealthBar)
        mShader.BindHealthBarPipeline(AlphaBlend);
    else if (Material == BatchMaterial::SpriteEffect)
        mShader.BindSpriteEffectPipeline(AlphaBlend);
    else if (Material == BatchMaterial::PBRSprite)
        mShader.BindPBRSpritePipeline(AlphaBlend, mPBRSpriteParams.PBRConfig[2] > 0.5f && mPBRSpriteParams.PBRConfig[2] < 1.5f);
    else
        mShader.BindPipeline(AlphaBlend);
}

void Draw2D::SetHealthBarParams(float Fraction, Color Fill)
{
    Fraction = std::clamp(Fraction, 0.0f, 1.0f);
    auto ToFloat = [](uint8_t V) { return (float)V / 255.0f; };
    HealthBarShaderParams NewParams = {};
    NewParams.SolidColor[0] = ToFloat(Fill.R);
    NewParams.SolidColor[1] = ToFloat(Fill.G);
    NewParams.SolidColor[2] = ToFloat(Fill.B);
    NewParams.SolidColor[3] = ToFloat(Fill.A);
    NewParams.MultiplyColor[0] = Fraction;
    NewParams.MultiplyColor[1] = Fraction;
    NewParams.MultiplyColor[2] = 0.0f;
    NewParams.MultiplyColor[3] = 1.0f;
    NewParams.TeamColor[0] = ToFloat(Fill.R);
    NewParams.TeamColor[1] = ToFloat(Fill.G);
    NewParams.TeamColor[2] = ToFloat(Fill.B);
    NewParams.TeamColor[3] = ToFloat(Fill.A);

    if (mCurrentMaterial != BatchMaterial::HealthBar ||
        std::memcmp(&mHealthBarParams, &NewParams, sizeof(NewParams)) != 0)
    {
        Flush();
        mHealthBarParams = NewParams;
    }
}

void Draw2D::SetSpriteEffectParams(float Hallucination,
                                   float Cloaked,
                                   float Brighten,
                                   float Shadow,
                                   float WarpTexture,
                                   float WarpScaleX,
                                   float WarpScaleY,
                                   Color TeamColor,
                                   bool UseTeamColor)
{
    auto ToFloat = [](uint8_t V) { return (float)V / 255.0f; };
    SpriteEffectShaderParams NewParams = {};
    NewParams.MultiplyColor[0] = 1.0f;
    NewParams.MultiplyColor[1] = 1.0f;
    NewParams.MultiplyColor[2] = 1.0f;
    NewParams.MultiplyColor[3] = 1.0f;
    NewParams.TeamColor[0] = ToFloat(TeamColor.R);
    NewParams.TeamColor[1] = ToFloat(TeamColor.G);
    NewParams.TeamColor[2] = ToFloat(TeamColor.B);
    NewParams.TeamColor[3] = ToFloat(TeamColor.A);
    NewParams.EffectData[0] = std::clamp(Hallucination, 0.0f, 1.0f);
    NewParams.EffectData[1] = std::clamp(Cloaked, 0.0f, 1.0f);
    NewParams.EffectData[2] = std::clamp(Brighten, 0.0f, 1.0f);
    NewParams.EffectData[3] = UseTeamColor ? 1.0f : 0.0f;
    NewParams.EffectMode[0] = std::clamp(Shadow, 0.0f, 1.0f);
    NewParams.EffectMode[1] = std::clamp(WarpTexture, 0.0f, 1.0f);
    NewParams.EffectMode[2] = std::max(0.0f, WarpScaleX);
    NewParams.EffectMode[3] = std::max(0.0f, WarpScaleY);

    if (mCurrentMaterial != BatchMaterial::SpriteEffect ||
        std::memcmp(&mSpriteEffectParams, &NewParams, sizeof(NewParams)) != 0)
    {
        Flush();
        mSpriteEffectParams = NewParams;
    }
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
    mHealthBarParams = {};
    mSpriteEffectParams = {};
    mVerticesInBatch = 0;
    mIndicesInBatch = 0;
    mFlushCountInFrame = 0;
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
    mHealthBarParams = {};
    mSpriteEffectParams = {};
    mVerticesInBatch = 0;
    mIndicesInBatch = 0;
    mFlushCountInFrame = 0;
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
    ++mFlushCountInFrame;
    if (!mCurrentVbuf.Valid() || !mCurrentIbuf.Valid())
    {
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
    else if (mCurrentMaterial == BatchMaterial::PBRSprite)
    {
        mShader.ApplyPBRSpriteParams(mPBRSpriteParams);
    }

    // Bind pipeline + resources
    sg_bindings B = {};
    B.vertex_buffers[0] = Render::ToSokol(mCurrentVbuf.Buffer);
    B.vertex_buffer_offsets[0] = VertexOffset;
    B.index_buffer = Render::ToSokol(mCurrentIbuf.Buffer);
    B.index_buffer_offset = IndexOffset;
    if (mCurrentMaterial == BatchMaterial::Textured ||
        mCurrentMaterial == BatchMaterial::SpriteEffect ||
        mCurrentMaterial == BatchMaterial::PBRSprite)
    {
        B.views[0] = Render::ToSokol(mCurrentTexture.View);
        B.samplers[0] = Render::ToSokol(mShader.GetSampler().Sampler);
        if (mCurrentMaterial == BatchMaterial::SpriteEffect)
        {
            B.views[1] = Render::ToSokol(
                mCurrentTeamColorMask.Valid() ? mCurrentTeamColorMask.View : mWhiteTexture.View);
        }
        else if (mCurrentMaterial == BatchMaterial::PBRSprite)
        {
            B.views[1] = Render::ToSokol(
                mCurrentNormalTeamAO.Valid() ? mCurrentNormalTeamAO.View : mWhiteTexture.View);
            B.views[2] = Render::ToSokol(
                mCurrentSpecEmissive.Valid() ? mCurrentSpecEmissive.View : mWhiteTexture.View);
        }
    }
    sg_apply_bindings(&B);

    sg_draw(0, mIndicesInBatch, 1);

    mVerticesInBatch = 0;
    mIndicesInBatch = 0;
}

void Draw2D::EndFrame()
{
    if (mActiveQueue)
    {
        RenderQueue2D* Queue = mActiveQueue;
        mActiveQueue = nullptr;
        Queue->Playback(*this);
    }
    Flush();
}

void Draw2D::DrawSprite(const GpuTextureHandle& Texture,
                        float X, float Y, float W, float H,
                        float U0, float V0, float U1, float V1,
                        Color Tint, bool FlipX)
{
    if (!Texture.Valid()) return;
    if (mActiveQueue)
    {
        mActiveQueue->SubmitSprite(Texture, X, Y, W, H, U0, V0, U1, V1, Tint, FlipX);
        return;
    }
    if (mCurrentTexture.Image.id != Texture.Image.id ||
        mCurrentTexture.View.id != Texture.View.id ||
        mCurrentMaterial != BatchMaterial::Textured)
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
                              float WarpTexture,
                              float WarpScaleX,
                              float WarpScaleY,
                              Color TeamColor,
                              const GpuTextureHandle* EffectMask)
{
    if (!Texture.Valid()) return;
    if (mActiveQueue)
    {
        mActiveQueue->SubmitSpriteEffect(
            Texture, X, Y, W, H, U0, V0, U1, V1, Tint, FlipX,
            Hallucination, Cloaked, Brighten, Shadow, WarpTexture,
            WarpScaleX, WarpScaleY, TeamColor, EffectMask);
        return;
    }
    const bool UseWarpTexture = WarpTexture > 0.0f && EffectMask && EffectMask->Valid();
    const bool UseTeamColor = !UseWarpTexture && EffectMask && EffectMask->Valid();
    if (mCurrentTexture.Image.id != Texture.Image.id ||
        mCurrentTexture.View.id != Texture.View.id ||
        mCurrentMaterial != BatchMaterial::SpriteEffect)
    {
        Flush();
        mCurrentTexture = Texture;
    }
    const bool UseEffectMask = UseTeamColor || UseWarpTexture;
    if (UseEffectMask &&
        (mCurrentTeamColorMask.Image.id != EffectMask->Image.id ||
         mCurrentTeamColorMask.View.id != EffectMask->View.id))
    {
        Flush();
        mCurrentTeamColorMask = *EffectMask;
    }
    else if (!UseEffectMask && mCurrentTeamColorMask.Valid())
    {
        Flush();
        mCurrentTeamColorMask = {};
    }
    SetSpriteEffectParams(Hallucination, Cloaked, Brighten, Shadow,
                          UseWarpTexture ? 1.0f : 0.0f,
                          WarpScaleX, WarpScaleY,
                          TeamColor, UseTeamColor);
    SetPipeline(true, BatchMaterial::SpriteEffect);
    AddQuad(X, Y, W, H, U0, V0, U1, V1, Tint, FlipX);
}

void Draw2D::DrawSpriteFromImage(const GpuTextureHandle& Texture,
                                 float X, float Y, float W, float H, Color Tint)
{
    DrawSprite(Texture, X, Y, W, H, 0.0f, 0.0f, 1.0f, 1.0f, Tint, false);
}

void Draw2D::DrawPBRSprite(const GpuTextureHandle& BaseColorA,
                           const GpuTextureHandle* NormalTeamAO,
                           const GpuTextureHandle* SpecEmissive,
                           float X, float Y, float W, float H,
                           float U0, float V0, float U1, float V1,
                           Color Tint,
                           Color TeamColor,
                           bool FlipX,
                           int BlendMode)
{
    if (!BaseColorA.Valid()) return;
    const bool HasNTA = NormalTeamAO && NormalTeamAO->Valid();
    const bool HasSE  = SpecEmissive && SpecEmissive->Valid();

    PBRSpriteShaderParams NewParams = {};
    NewParams.TeamColor[0] = (float)TeamColor.R / 255.0f;
    NewParams.TeamColor[1] = (float)TeamColor.G / 255.0f;
    NewParams.TeamColor[2] = (float)TeamColor.B / 255.0f;
    NewParams.TeamColor[3] = (float)TeamColor.A / 255.0f;
    NewParams.LightDir[0] = 0.37f;
    NewParams.LightDir[1] = 0.09f;
    NewParams.LightDir[2] = 0.92f;
    NewParams.LightDir[3] = FlipX ? -1.0f : 1.0f;
    NewParams.PBRConfig[0] = HasNTA ? 1.0f : 0.0f;
    NewParams.PBRConfig[1] = HasSE  ? 1.0f : 0.0f;
    NewParams.PBRConfig[2] = (float)BlendMode;
    NewParams.PBRConfig[3] = (float)Tint.A / 255.0f;

    const GpuTextureHandle TargetNTA = HasNTA ? *NormalTeamAO : GpuTextureHandle{};
    const GpuTextureHandle TargetSE  = HasSE  ? *SpecEmissive : GpuTextureHandle{};

    if (mCurrentMaterial != BatchMaterial::PBRSprite ||
        mCurrentTexture.Image.id != BaseColorA.Image.id ||
        mCurrentNormalTeamAO.Image.id != TargetNTA.Image.id ||
        mCurrentSpecEmissive.Image.id != TargetSE.Image.id ||
        std::memcmp(&mPBRSpriteParams, &NewParams, sizeof(NewParams)) != 0)
    {
        Flush();
        mCurrentTexture = BaseColorA;
        mCurrentNormalTeamAO = TargetNTA;
        mCurrentSpecEmissive = TargetSE;
        mPBRSpriteParams = NewParams;
    }

    SetPipeline(true, BatchMaterial::PBRSprite);
    AddQuad(X, Y, W, H, U0, V0, U1, V1, Tint, false);
    if (mVerticesInBatch + 4 > kMaxBatchVerts) Flush();
}

void Draw2D::DrawRect(float X, float Y, float W, float H, Color C)
{
    if (mActiveQueue)
    {
        mActiveQueue->SubmitRect(X, Y, W, H, C);
        return;
    }
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
    if (mActiveQueue)
    {
        mActiveQueue->SubmitHealthBar(X, Y, W, H, Fraction, Fill);
        return;
    }
    SetHealthBarParams(Fraction, Fill);
    SetPipeline(true, BatchMaterial::HealthBar);
    AddQuad(X, Y, W, H, 0.0f, 0.0f, 1.0f, 1.0f, Fill, false);
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
    if (Text.empty()) return;
    if (mActiveQueue)
    {
        mActiveQueue->SubmitText(Text, X, Y, Size, C);
        return;
    }

    // Adapt font scale: values <= 4.0f (e.g. 1.0f, 1.15f, 1.9f) are treated as multipliers of base size 12px.
    float EffectiveSize = Size;
    if (EffectiveSize <= 4.0f)
    {
        EffectiveSize *= 12.0f;
    }

    float CurX = X;
    float CurY = Y;
    size_t Index = 0;

    while (Index < Text.size())
    {
        if (Text[Index] == '\r')
        {
            ++Index;
            continue;
        }
        if (Text[Index] == '\n')
        {
            CurY += EffectiveSize;
            CurX = X;
            ++Index;
            continue;
        }

        uint32_t Codepoint = NextUtf8Codepoint(Text, Index);
        if (Codepoint == 0) break;

        float UV[4] = {0};
        bool IsWide = false;
        if (!g_Font.GetGlyphUV(Codepoint, UV, IsWide))
        {
            if (!g_Font.GetGlyphUV('?', UV, IsWide))
                continue;
        }

        // Texture handle may change if a new Unicode glyph dynamically expanded the atlas.
        mFontTexture = g_Font.GetTexture();
        if (!mFontTexture.Valid()) return;

        if (mCurrentTexture.Image.id != mFontTexture.Image.id ||
            mCurrentTexture.View.id != mFontTexture.View.id ||
            mCurrentMaterial != BatchMaterial::Textured)
        {
            Flush();
            mCurrentTexture = mFontTexture;
        }
        SetPipeline(true, BatchMaterial::Textured);

        float CW = IsWide ? EffectiveSize : (EffectiveSize * (float)BitmapFont::CellW / (float)BitmapFont::CellH);
        AddQuad(CurX, CurY, CW, EffectiveSize, UV[0], UV[1], UV[2], UV[3], C, false);
        CurX += CW;

        if (mVerticesInBatch + 4 > kMaxBatchVerts) { Flush(); }
    }
}

} // namespace Render
