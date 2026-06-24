#pragma once

#include "Render/sokol/Color.h"
#include "Render/sokol/Camera2D.h"
#include "Render/sokol/Shader2D.h"
#include "Resource/sokol/GrpToSokol.h"

#include "third_party/sokol/sokol_gfx.h"
#include "third_party/HandmadeMath.h"

#include <cstdint>
#include <string>
#include <vector>
#include <array>

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
    void DrawSprite(sg_image Image, sg_view View,
                    float X, float Y, float W, float H,
                    float U0 = 0.0f, float V0 = 0.0f,
                    float U1 = 1.0f, float V1 = 1.0f,
                    Color Tint = Color(255, 255, 255, 255),
                    bool FlipX = false);

    // Solid color rectangle (no texture; uses the 1x1 white image)
    void DrawRect(float X, float Y, float W, float H, Color C);
    void DrawRectLines(float X, float Y, float W, float H, Color C, float Thickness = 1.0f);

    // Text (simple bitmap font). Draws left-to-right.
    void DrawText(const std::string& Text, float X, float Y, float Size, Color C);

    // Draw filled quad from a SokolImage struct
    void DrawSpriteFromImage(const Resource::SokolBridge::SokolImage& Img,
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

    void Flush();
    void EnsureCapacity(int Vertices, int Indices);
    void AddQuad(float X, float Y, float W, float H,
                 float U0, float V0, float U1, float V1,
                 Color C, bool FlipX);
    void SetPipeline(bool AlphaBlend);

    Shader2D mShader;
    sg_buffer mVbuf = {0};
    sg_buffer mIbuf = {0};
    sg_buffer mVbufScreen = {0};
    sg_buffer mIbufScreen = {0};
    sg_buffer mCurrentVbuf = {0};
    sg_buffer mCurrentIbuf = {0};
    bool mInScreenPass = false;

    std::vector<Vertex> mVertices;
    std::vector<uint16_t> mIndices;

    // Batched draw state
    sg_image mCurrentImage = {0};
    sg_view mCurrentView = {0};
    bool mCurrentAlphaBlend = true;
    int mVerticesInBatch = 0;
    int mIndicesInBatch = 0;

    sg_image mWhiteImage = {0};  // 1x1 white
    sg_view mWhiteView = {0};

    // Built-in font
    sg_image mFontImage = {0};
    sg_view mFontView = {0};
    int mFontCharWidth = 0;
    int mFontCharHeight = 0;

    int mFrameWidth = 0;
    int mFrameHeight = 0;

    HMM_Mat4 mCurrentProjection = HMM_M4D(1.0f);
};

extern Draw2D g_Draw2D;

} // namespace Render
