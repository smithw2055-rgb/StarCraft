#pragma once

#include "Core/Color.h"
#include "Render/Camera2D.h"
#include "Render/GpuTextureHandle.h"
#include "Render/Shader2D.h"
#include "Render/RenderQueue2D.h"

#include "third_party/HandmadeMath.h"

#include <cstdint>
#include <string>
#include <vector>
#include <array>

#ifdef DrawText
#undef DrawText
#endif

namespace Render
{

class Draw2D
{
public:
    bool Init();
    void Shutdown();

    // Begin a 2D draw pass.  Sets the active projection from the camera.
    void BeginFrame(const Camera2D& Camera);
    // Begin a 2D draw pass in screen space (identity ortho, top-left origin = screen pixel).
    void BeginFrameScreen();
    void EndFrame();

    // Attach / detach a deferred RenderQueue2D for 64-bit SortKey command recording.
    void SetActiveQueue(RenderQueue2D* Queue) { mActiveQueue = Queue; }
    RenderQueue2D* GetActiveQueue() const { return mActiveQueue; }

    void SetSortContext(RenderLayer2D Layer,
                        RenderSubBand2D SubBand = RenderSubBand2D::RootSprite,
                        uint16_t DepthKey = 0x8000u)
    {
        if (mActiveQueue)
            mActiveQueue->SetSortContext(Layer, SubBand, DepthKey);
    }

    void SetSortContext(uint8_t Layer,
                        uint8_t SubBand = static_cast<uint8_t>(RenderSubBand2D::RootSprite),
                        uint16_t DepthKey = 0x8000u)
    {
        if (mActiveQueue)
            mActiveQueue->SetSortContext(Layer, SubBand, DepthKey);
    }

    // Sprites / textured quads

    void DrawSprite(const GpuTextureHandle& Texture,
                    float X, float Y, float W, float H,
                    float U0 = 0.0f, float V0 = 0.0f,
                    float U1 = 1.0f, float V1 = 1.0f,
                    Color Tint = Color(255, 255, 255, 255),
                    bool FlipX = false);
    void DrawSpriteEffect(const GpuTextureHandle& Texture,
                          float X, float Y, float W, float H,
                          float U0, float V0, float U1, float V1,
                          Color Tint,
                          bool FlipX,
                          float Hallucination,
                          float Cloaked,
                          float Brighten = 0.0f,
                          float Shadow = 0.0f,
                          float WarpTexture = 0.0f,
                          float WarpScaleX = 1.0f,
                          float WarpScaleY = 1.0f,
                          Color TeamColor = Color(255, 255, 255, 255),
                          const GpuTextureHandle* EffectMask = nullptr);

    void DrawPBRSprite(const GpuTextureHandle& BaseColorA,
                       const GpuTextureHandle* NormalTeamAO,
                       const GpuTextureHandle* SpecEmissive,
                       float X, float Y, float W, float H,
                       float U0 = 0.0f, float V0 = 0.0f,
                       float U1 = 1.0f, float V1 = 1.0f,
                       Color Tint = Color(255, 255, 255, 255),
                       Color TeamColor = Color(31, 122, 255, 255),
                       bool FlipX = false,
                       int BlendMode = 0);

    // Solid color rectangle (no texture; uses the 1x1 white image)
    void DrawRect(float X, float Y, float W, float H, Color C);
    void DrawRectLines(float X, float Y, float W, float H, Color C, float Thickness = 1.0f);
    void DrawHealthBar(float X, float Y, float W, float H, float Fraction, Color Fill);

    // Text (simple bitmap font). Draws left-to-right.
    void DrawText(const std::string& Text, float X, float Y, float Size, Color C);

    // Draw filled quad from a texture handle
    void DrawSpriteFromImage(const GpuTextureHandle& Texture,
                             float X, float Y, float W, float H,
                             Color Tint = Color(255, 255, 255, 255));

    uint32_t GetFlushCountInFrame() const { return mFlushCountInFrame; }
    void ResetFlushCountInFrame() { mFlushCountInFrame = 0; }

private:
    struct Vertex
    {
        float X;
        float Y;
        float U;
        float V;
        uint32_t C; // RGBA8 packed
    };

    enum class BatchMaterial
    {
        Textured,
        HealthBar,
        SpriteEffect,
        PBRSprite
    };

    void Flush();
    void EnsureCapacity(int Vertices, int Indices);
    void AddQuad(float X, float Y, float W, float H,
                 float U0, float V0, float U1, float V1,
                 Color C, bool FlipX);
    void SetPipeline(bool AlphaBlend, BatchMaterial Material = BatchMaterial::Textured);
    void SetHealthBarParams(float Fraction, Color Fill);
    void SetSpriteEffectParams(float Hallucination,
                               float Cloaked,
                               float Brighten,
                               float Shadow,
                               float WarpTexture,
                               float WarpScaleX,
                               float WarpScaleY,
                               Color TeamColor,
                               bool UseTeamColor);

    Shader2D mShader;
    GpuBufferHandle mVbuf;
    GpuBufferHandle mIbuf;
    GpuBufferHandle mVbufScreen;
    GpuBufferHandle mIbufScreen;
    GpuBufferHandle mCurrentVbuf;
    GpuBufferHandle mCurrentIbuf;
    bool mInScreenPass = false;

    std::vector<Vertex> mVertices;
    std::vector<uint16_t> mIndices;

    // Batched draw state
    GpuTextureHandle mCurrentTexture;
    bool mCurrentAlphaBlend = true;
    BatchMaterial mCurrentMaterial = BatchMaterial::Textured;
    HealthBarShaderParams mHealthBarParams = {};
    SpriteEffectShaderParams mSpriteEffectParams = {};
    PBRSpriteShaderParams mPBRSpriteParams = {};
    GpuTextureHandle mCurrentTeamColorMask;
    GpuTextureHandle mCurrentNormalTeamAO;
    GpuTextureHandle mCurrentSpecEmissive;
    int mVerticesInBatch = 0;
    int mIndicesInBatch = 0;
    uint32_t mFlushCountInFrame = 0;

    RenderQueue2D* mActiveQueue = nullptr;

    GpuTextureHandle mWhiteTexture;  // 1x1 white

    // Built-in font
    GpuTextureHandle mFontTexture;
    int mFontCharWidth = 0;
    int mFontCharHeight = 0;

    int mFrameWidth = 0;
    int mFrameHeight = 0;

    HMM_Mat4 mCurrentProjection = HMM_M4D(1.0f);
};

inline void RenderQueue2D::Playback(Draw2D& Target)
{
    Sort();
    RenderQueue2D* PrevQueue = Target.GetActiveQueue();
    Target.SetActiveQueue(nullptr);

    for (const RenderCommand2D& Cmd : mCommands)
    {
        switch (Cmd.Type)
        {
            case RenderCommandType2D::Sprite:
                Target.DrawSprite(
                    Cmd.Texture, Cmd.X, Cmd.Y, Cmd.W, Cmd.H,
                    Cmd.U0, Cmd.V0, Cmd.U1, Cmd.V1,
                    Cmd.Tint, Cmd.FlipX);
                break;

            case RenderCommandType2D::SpriteEffect:
                Target.DrawSpriteEffect(
                    Cmd.Texture, Cmd.X, Cmd.Y, Cmd.W, Cmd.H,
                    Cmd.U0, Cmd.V0, Cmd.U1, Cmd.V1,
                    Cmd.Tint, Cmd.FlipX,
                    Cmd.Hallucination, Cmd.Cloaked, Cmd.Brighten,
                    Cmd.Shadow, Cmd.WarpTexture,
                    Cmd.WarpScaleX, Cmd.WarpScaleY,
                    Cmd.TeamColor,
                    Cmd.HasEffectMask ? &Cmd.EffectMask : nullptr);
                break;

            case RenderCommandType2D::Rect:
                Target.DrawRect(Cmd.X, Cmd.Y, Cmd.W, Cmd.H, Cmd.Tint);
                break;

            case RenderCommandType2D::HealthBar:
                Target.DrawHealthBar(
                    Cmd.X, Cmd.Y, Cmd.W, Cmd.H, Cmd.Fraction, Cmd.Tint);
                break;

            case RenderCommandType2D::Text:
                if (Cmd.TextPayloadIndex < mTextPayloads.size())
                {
                    Target.DrawText(
                        mTextPayloads[Cmd.TextPayloadIndex],
                        Cmd.X, Cmd.Y, Cmd.FontSize, Cmd.Tint);
                }
                break;
        }
    }

    Target.SetActiveQueue(PrevQueue);
}

extern Draw2D g_Draw2D;

void UploadPendingFontGlyphs();

} // namespace Render

// === Consolidated from src/Render/Renderer.h ===
#include <string>

namespace Render
{

bool Initialize();
void Shutdown();
void BeginFrame();
void EndPass();
void PrepareNativeOverlay();
void Commit();
void EndFrame();
void ResetStateCache();
bool IsActive();
void SetClearColor(float R, float G, float B, float A = 1.0f);
bool CaptureScreenshot(const std::string& OutputPath);

} // namespace Render


