#pragma once

#include "Render/Color.h"
#include "Render/Camera2D.h"
#include "Render/GpuBufferHandle.h"
#include "Render/Shader2D.h"
#include "Render/GpuTextureHandle.h"

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
        SpriteEffect
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
    GpuTextureHandle mCurrentTeamColorMask;
    int mVerticesInBatch = 0;
    int mIndicesInBatch = 0;

    GpuTextureHandle mWhiteTexture;  // 1x1 white

    // Built-in font
    GpuTextureHandle mFontTexture;
    int mFontCharWidth = 0;
    int mFontCharHeight = 0;

    int mFrameWidth = 0;
    int mFrameHeight = 0;

    HMM_Mat4 mCurrentProjection = HMM_M4D(1.0f);
};

extern Draw2D g_Draw2D;

} // namespace Render
