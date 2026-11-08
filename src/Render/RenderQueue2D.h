#pragma once

#include "Core/Color.h"
#include "Render/GpuTextureHandle.h"
#include "Engine/Node2D.h"
#include "Resource/ModernAssetRegistry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Render
{

class Draw2D;

enum class RenderLayerPass : uint8_t {
    TerrainMegatile = 0,
    GroundShadow = 1,
    GroundSelection = 2,
    GroundUnit = 3,
    GroundOverlay = 4,
    AirShadow = 5,
    AirUnit = 6,
    AirOverlay = 7,
    UI = 8
};

enum class PBRBlendMode : uint8_t {
    NormalDeferredLit = 0,
    AdditiveEmissive = 1,
    AlphaBlendShadow = 2,
    CloakRefraction = 3,
    TeamColorRemap = 4
};

struct PBRSpriteMaterialDescriptor {
    uint16_t MaterialID = 0;
    uint32_t ImageID = 0;
    PBRBlendMode BlendMode = PBRBlendMode::NormalDeferredLit;
    bool HasNormalTeamAOTexture = false;
    bool HasSpecEmissiveTexture = false;
    std::string ShaderName = "StarCraft/HD_Deferred_Sprite_PBR";
    std::string BaseColorTexture;
    std::string NormalTeamAOTexture;
    std::string SpecEmissiveTexture;
};

struct RenderBatchStats {
    size_t TotalCommands = 0;
    size_t DrawCalls = 0;
    size_t PBRDeferredLitCommands = 0;
    size_t AdditiveGlowCommands = 0;
    size_t ShadowCommands = 0;
};

enum class RenderLayer2D : uint8_t
{
    Terrain          = 10,
    GroundPowerField = 20,
    SelectionOverlay = 30,
    RallyOverlay     = 35,
    GroundEffect     = 40,
    GroundEntities   = 50,
    AirEntities      = 60,
    TopEffects       = 70,
    Projectiles      = 80,
    StatusBars       = 90,
    DebugOverlay     = 100,
    ScreenUI         = 200
};

enum class RenderSubBand2D : uint8_t
{
    StatusBarBorder = 0,
    StatusBarFill   = 1,
    Shadow          = 32,
    UnderNode       = 64,
    RootSprite      = 128,
    OverNode        = 192
};

enum class RenderMaterialKind : uint8_t
{
    Textured     = 0,
    SpriteEffect = 1,
    HealthBar    = 2
};

// ============================================================================
// SortKey2D: 64-bit deterministic rendering sort key
// Layout:
//   [63:56] Layer            (8 bits)  - Coarse render pass / elevation layer
//   [55:48] SubBand          (8 bits)  - Sub-layer (shadow / under / root / over / border vs fill)
//   [47:32] DepthKey         (16 bits) - Quantized top-down Y + ZOrder painter's depth
//   [31:28] Material         (4 bits)  - Pipeline material kind (Textured / SpriteEffect / HealthBar)
//   [27:12] TextureStateKey  (16 bits) - Texture view ID / shader parameter hash for batch coalescing
//   [11:0]  Sequence         (12 bits) - Monotonic sequence within bucket
// ============================================================================
struct SortKey2D
{
    uint64_t Value = 0;

    static constexpr uint64_t Pack(uint8_t Layer,
                                   uint8_t SubBand,
                                   uint16_t DepthKey,
                                   RenderMaterialKind Material,
                                   uint16_t TextureStateKey,
                                   uint16_t Sequence)
    {
        return (static_cast<uint64_t>(Layer) << 56) |
               (static_cast<uint64_t>(SubBand) << 48) |
               (static_cast<uint64_t>(DepthKey) << 32) |
               (static_cast<uint64_t>(static_cast<uint8_t>(Material) & 0x0Fu) << 28) |
               (static_cast<uint64_t>(TextureStateKey) << 12) |
               (static_cast<uint64_t>(Sequence & 0x0FFFu));
    }

    static uint16_t EncodeDepthKey(float WorldY, int ZOrder = 0)
    {
        // Bias world Y by +4096 pixels and quantize at 0.5-pixel resolution,
        // with ZOrder scaled by 64 sub-units.
        const float Biased = (WorldY + 4096.0f) * 2.0f + static_cast<float>(ZOrder * 64);
        const int Quantized = static_cast<int>(std::lround(Biased));
        return static_cast<uint16_t>(std::clamp(Quantized, 0, 65535));
    }

    static uint16_t HashTextureState(const GpuTextureHandle& Tex)
    {
        uint32_t H = Tex.View.id ^ (Tex.Image.id * 2654435761u);
        H ^= (H >> 16);
        return static_cast<uint16_t>(H & 0xFFFFu);
    }

    static uint16_t HashHealthBarState(float Fraction, Color Fill)
    {
        const uint32_t QuantFrac = static_cast<uint32_t>(
            std::clamp(static_cast<int>(std::lround(Fraction * 1024.0f)), 0, 1024));
        const uint32_t PackedColor =
            static_cast<uint32_t>(Fill.R) |
            (static_cast<uint32_t>(Fill.G) << 8) |
            (static_cast<uint32_t>(Fill.B) << 16) |
            (static_cast<uint32_t>(Fill.A) << 24);
        uint32_t H = PackedColor * 2654435761u ^ (QuantFrac * 40503u);
        H ^= (H >> 16);
        return static_cast<uint16_t>(H & 0xFFFFu);
    }

    static uint16_t HashSpriteEffectState(const GpuTextureHandle& Tex,
                                          const GpuTextureHandle& Mask,
                                          Color TeamColor,
                                          float Hallucination,
                                          float Cloaked,
                                          float Brighten,
                                          float Shadow,
                                          float WarpTexture)
    {
        uint32_t H = (Tex.View.id * 2654435761u) ^ (Mask.View.id * 2246822519u);
        const uint32_t PackedTeam =
            static_cast<uint32_t>(TeamColor.R) |
            (static_cast<uint32_t>(TeamColor.G) << 8) |
            (static_cast<uint32_t>(TeamColor.B) << 16) |
            (static_cast<uint32_t>(TeamColor.A) << 24);
        H ^= PackedTeam * 3266489917u;
        uint32_t Flags = 0;
        if (Hallucination > 0.0f) Flags |= 1u << 0;
        if (Cloaked > 0.0f)       Flags |= 1u << 1;
        if (Brighten > 0.0f)      Flags |= 1u << 2;
        if (Shadow > 0.0f)        Flags |= 1u << 3;
        if (WarpTexture > 0.0f)   Flags |= 1u << 4;
        H ^= (Flags * 668265263u);
        H ^= (H >> 16);
        return static_cast<uint16_t>(H & 0xFFFFu);
    }

    static constexpr uint8_t DecodeLayer(uint64_t Key)
    {
        return static_cast<uint8_t>((Key >> 56) & 0xFFu);
    }

    static constexpr uint8_t DecodeSubBand(uint64_t Key)
    {
        return static_cast<uint8_t>((Key >> 48) & 0xFFu);
    }

    static constexpr uint16_t DecodeDepthKey(uint64_t Key)
    {
        return static_cast<uint16_t>((Key >> 32) & 0xFFFFu);
    }

    static constexpr RenderMaterialKind DecodeMaterial(uint64_t Key)
    {
        return static_cast<RenderMaterialKind>((Key >> 28) & 0x0Fu);
    }

    static constexpr uint16_t DecodeTextureStateKey(uint64_t Key)
    {
        return static_cast<uint16_t>((Key >> 12) & 0xFFFFu);
    }

    static constexpr uint16_t DecodeSequence(uint64_t Key)
    {
        return static_cast<uint16_t>(Key & 0x0FFFu);
    }
};

struct SortContext2D
{
    uint8_t Layer = static_cast<uint8_t>(RenderLayer2D::GroundEntities);
    uint8_t SubBand = static_cast<uint8_t>(RenderSubBand2D::RootSprite);
    uint16_t DepthKey = 0x8000u;
};

enum class RenderCommandType2D : uint8_t
{
    Sprite = 0,
    SpriteEffect = 1,
    Rect = 2,
    HealthBar = 3,
    Text = 4
};

struct RenderCommand2D
{
    uint64_t SortKey = 0;
    uint32_t SubmissionOrder = 0;
    RenderCommandType2D Type = RenderCommandType2D::Sprite;

    GpuTextureHandle Texture = {};
    GpuTextureHandle EffectMask = {};
    bool HasEffectMask = false;

    float X = 0.0f;
    float Y = 0.0f;
    float W = 0.0f;
    float H = 0.0f;
    float U0 = 0.0f;
    float V0 = 0.0f;
    float U1 = 1.0f;
    float V1 = 1.0f;

    Color Tint = Color(255, 255, 255, 255);
    Color TeamColor = Color(255, 255, 255, 255);
    bool FlipX = false;

    // SpriteEffect / HealthBar parameters
    float Fraction = 1.0f;
    float Hallucination = 0.0f;
    float Cloaked = 0.0f;
    float Brighten = 0.0f;
    float Shadow = 0.0f;
    float WarpTexture = 0.0f;
    float WarpScaleX = 1.0f;
    float WarpScaleY = 1.0f;

    // Text payload index into RenderQueue2D::mTextPayloads
    uint32_t TextPayloadIndex = UINT32_MAX;
    float FontSize = 12.0f;

    // PBR Sprite & SceneNode2D fields
    RenderLayerPass Pass = RenderLayerPass::GroundUnit;
    uint8_t ElevationLayer = 4;
    Core::Math::Vec2 WorldPosition{0.0f, 0.0f};
    uint32_t ImageID = 0;
    int32_t FrameSet = 0;
    int32_t Direction17 = 0;
    bool FlipHorizontal = false;
    uint8_t TeamColorID = 0;
    PBRSpriteMaterialDescriptor Material;

    static uint64_t ComputeSortKey(
        RenderLayerPass Pass,
        uint8_t ElevationLayer,
        float WorldY,
        uint16_t MaterialID,
        int16_t ZIndexOffset)
    {
        const uint64_t PassBits = static_cast<uint64_t>(Pass) & 0xFFULL;
        const uint64_t ElevBits = static_cast<uint64_t>(ElevationLayer) & 0xFFULL;
        const int32_t ClampedY = std::clamp(static_cast<int32_t>(WorldY * 4.0f) + 32768, 0, 65535);
        const uint64_t YBits = static_cast<uint64_t>(ClampedY) & 0xFFFFULL;
        const uint64_t MatBits = static_cast<uint64_t>(MaterialID) & 0xFFFFULL;
        const uint64_t ZBits = static_cast<uint64_t>(static_cast<int32_t>(ZIndexOffset) + 32768) & 0xFFFFULL;

        return (PassBits << 56) | (ElevBits << 48) | (YBits << 32) | (MatBits << 16) | ZBits;
    }
};

struct RenderQueueStats
{
    uint32_t TotalSubmitted = 0;
    uint32_t CulledCommands = 0;
    uint32_t RecordedCommands = 0;
    uint32_t UnsortedFlushes = 0;
    uint32_t SortedFlushes = 0;
    uint32_t StateChangesSaved = 0;
};

class RenderQueue2D
{
public:
    void Clear()
    {
        mCommands.clear();
        mTextPayloads.clear();
        mNextSubmissionOrder = 0;
        mTotalSubmitted = 0;
        mCulledCommands = 0;
        mIsSorted = true;
    }

    void Reserve(size_t CommandCapacity)
    {
        mCommands.reserve(CommandCapacity);
    }

    void SetCullBounds(float MinX, float MinY, float MaxX, float MaxY, bool Enabled = true)
    {
        mCullMinX = MinX;
        mCullMinY = MinY;
        mCullMaxX = MaxX;
        mCullMaxY = MaxY;
        mCullEnabled = Enabled;
    }

    void DisableCulling()
    {
        mCullEnabled = false;
    }

    void SetSortContext(uint8_t Layer,
                        uint8_t SubBand = static_cast<uint8_t>(RenderSubBand2D::RootSprite),
                        uint16_t DepthKey = 0x8000u)
    {
        mContext.Layer = Layer;
        mContext.SubBand = SubBand;
        mContext.DepthKey = DepthKey;
    }

    void SetSortContext(RenderLayer2D Layer,
                        RenderSubBand2D SubBand = RenderSubBand2D::RootSprite,
                        uint16_t DepthKey = 0x8000u)
    {
        SetSortContext(static_cast<uint8_t>(Layer),
                       static_cast<uint8_t>(SubBand),
                       DepthKey);
    }

    const SortContext2D& GetSortContext() const { return mContext; }

    void SubmitSprite(const GpuTextureHandle& Texture,
                      float X, float Y, float W, float H,
                      float U0 = 0.0f, float V0 = 0.0f,
                      float U1 = 1.0f, float V1 = 1.0f,
                      Color Tint = Color(255, 255, 255, 255),
                      bool FlipX = false)
    {
        ++mTotalSubmitted;
        if (!Texture.Valid())
            return;
        if (IsCulled(X, Y, W, H))
        {
            ++mCulledCommands;
            return;
        }

        const uint32_t Seq = mNextSubmissionOrder++;
        const uint16_t TexKey = SortKey2D::HashTextureState(Texture);
        // Within the same layer/subband/depth, group by texture so identical atlases batch.
        const uint16_t SeqBits = static_cast<uint16_t>(Seq & 0x0FFFu);

        RenderCommand2D Cmd;
        Cmd.SortKey = SortKey2D::Pack(
            mContext.Layer, mContext.SubBand, mContext.DepthKey,
            RenderMaterialKind::Textured, TexKey, SeqBits);
        Cmd.SubmissionOrder = Seq;
        Cmd.Type = RenderCommandType2D::Sprite;
        Cmd.Texture = Texture;
        Cmd.X = X;
        Cmd.Y = Y;
        Cmd.W = W;
        Cmd.H = H;
        Cmd.U0 = U0;
        Cmd.V0 = V0;
        Cmd.U1 = U1;
        Cmd.V1 = V1;
        Cmd.Tint = Tint;
        Cmd.FlipX = FlipX;

        mCommands.push_back(Cmd);
        mIsSorted = false;
    }

    void SubmitSpriteEffect(const GpuTextureHandle& Texture,
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
        ++mTotalSubmitted;
        if (!Texture.Valid())
            return;
        if (IsCulled(X, Y, W, H))
        {
            ++mCulledCommands;
            return;
        }

        const uint32_t Seq = mNextSubmissionOrder++;
        const GpuTextureHandle MaskHandle = (EffectMask && EffectMask->Valid()) ? *EffectMask : GpuTextureHandle{};
        const uint16_t StateKey = SortKey2D::HashSpriteEffectState(
            Texture, MaskHandle, TeamColor,
            Hallucination, Cloaked, Brighten, Shadow, WarpTexture);

        RenderCommand2D Cmd;
        Cmd.SortKey = SortKey2D::Pack(
            mContext.Layer, mContext.SubBand, mContext.DepthKey,
            RenderMaterialKind::SpriteEffect, StateKey, static_cast<uint16_t>(Seq & 0x0FFFu));
        Cmd.SubmissionOrder = Seq;
        Cmd.Type = RenderCommandType2D::SpriteEffect;
        Cmd.Texture = Texture;
        Cmd.EffectMask = MaskHandle;
        Cmd.HasEffectMask = MaskHandle.Valid();
        Cmd.X = X;
        Cmd.Y = Y;
        Cmd.W = W;
        Cmd.H = H;
        Cmd.U0 = U0;
        Cmd.V0 = V0;
        Cmd.U1 = U1;
        Cmd.V1 = V1;
        Cmd.Tint = Tint;
        Cmd.FlipX = FlipX;
        Cmd.Hallucination = Hallucination;
        Cmd.Cloaked = Cloaked;
        Cmd.Brighten = Brighten;
        Cmd.Shadow = Shadow;
        Cmd.WarpTexture = WarpTexture;
        Cmd.WarpScaleX = WarpScaleX;
        Cmd.WarpScaleY = WarpScaleY;
        Cmd.TeamColor = TeamColor;

        mCommands.push_back(Cmd);
        mIsSorted = false;
    }

    void SubmitRect(float X, float Y, float W, float H, Color C)
    {
        ++mTotalSubmitted;
        if (IsCulled(X, Y, W, H))
        {
            ++mCulledCommands;
            return;
        }

        const uint32_t Seq = mNextSubmissionOrder++;
        // Solid rects all share the 1x1 white texture (TextureStateKey = 0),
        // so all rects in the same Layer/SubBand/DepthKey coalesce into one draw call.
        RenderCommand2D Cmd;
        Cmd.SortKey = SortKey2D::Pack(
            mContext.Layer, mContext.SubBand, mContext.DepthKey,
            RenderMaterialKind::Textured, 0u, static_cast<uint16_t>(Seq & 0x0FFFu));
        Cmd.SubmissionOrder = Seq;
        Cmd.Type = RenderCommandType2D::Rect;
        Cmd.X = X;
        Cmd.Y = Y;
        Cmd.W = W;
        Cmd.H = H;
        Cmd.Tint = C;

        mCommands.push_back(Cmd);
        mIsSorted = false;
    }

    void SubmitHealthBar(float X, float Y, float W, float H, float Fraction, Color Fill)
    {
        ++mTotalSubmitted;
        if (IsCulled(X, Y, W, H))
        {
            ++mCulledCommands;
            return;
        }

        const uint32_t Seq = mNextSubmissionOrder++;
        const float ClampedFrac = std::clamp(Fraction, 0.0f, 1.0f);
        const uint16_t StateKey = SortKey2D::HashHealthBarState(ClampedFrac, Fill);
        // Group identical (Fraction, Fill) health bars regardless of sequence within the same pass!
        RenderCommand2D Cmd;
        Cmd.SortKey = SortKey2D::Pack(
            mContext.Layer, mContext.SubBand, mContext.DepthKey,
            RenderMaterialKind::HealthBar, StateKey, 0u);
        Cmd.SubmissionOrder = Seq;
        Cmd.Type = RenderCommandType2D::HealthBar;
        Cmd.X = X;
        Cmd.Y = Y;
        Cmd.W = W;
        Cmd.H = H;
        Cmd.Fraction = ClampedFrac;
        Cmd.Tint = Fill;

        mCommands.push_back(Cmd);
        mIsSorted = false;
    }

    void SubmitText(const std::string& Text, float X, float Y, float Size, Color C)
    {
        ++mTotalSubmitted;
        if (Text.empty())
            return;

        const uint32_t Seq = mNextSubmissionOrder++;
        const uint32_t PayloadIdx = static_cast<uint32_t>(mTextPayloads.size());
        mTextPayloads.push_back(Text);

        RenderCommand2D Cmd;
        Cmd.SortKey = SortKey2D::Pack(
            mContext.Layer, mContext.SubBand, mContext.DepthKey,
            RenderMaterialKind::Textured, 0xFFFFu, static_cast<uint16_t>(Seq & 0x0FFFu));
        Cmd.SubmissionOrder = Seq;
        Cmd.Type = RenderCommandType2D::Text;
        Cmd.X = X;
        Cmd.Y = Y;
        Cmd.FontSize = Size;
        Cmd.Tint = C;
        Cmd.TextPayloadIndex = PayloadIdx;

        mCommands.push_back(Cmd);
        mIsSorted = false;
    }

    void Sort()
    {
        if (mIsSorted)
            return;
        std::stable_sort(mCommands.begin(), mCommands.end(),
                         [](const RenderCommand2D& A, const RenderCommand2D& B)
                         {
                             if (A.SortKey != B.SortKey)
                                 return A.SortKey < B.SortKey;
                             return A.SubmissionOrder < B.SubmissionOrder;
                         });
        mIsSorted = true;
    }

    static bool CanCoalesce(const RenderCommand2D& A, const RenderCommand2D& B)
    {
        if (A.Type == RenderCommandType2D::Text || B.Type == RenderCommandType2D::Text)
            return A.Type == RenderCommandType2D::Text && B.Type == RenderCommandType2D::Text;

        const RenderMaterialKind MatA = SortKey2D::DecodeMaterial(A.SortKey);
        const RenderMaterialKind MatB = SortKey2D::DecodeMaterial(B.SortKey);
        if (MatA != MatB)
            return false;

        if (MatA == RenderMaterialKind::Textured)
        {
            if (A.Type == RenderCommandType2D::Rect && B.Type == RenderCommandType2D::Rect)
                return true;
            if (A.Type != B.Type)
                return false;
            return A.Texture.Image.id == B.Texture.Image.id &&
                   A.Texture.View.id == B.Texture.View.id;
        }

        if (MatA == RenderMaterialKind::HealthBar)
        {
            return std::fabs(A.Fraction - B.Fraction) <= 1e-4f &&
                   A.Tint.R == B.Tint.R &&
                   A.Tint.G == B.Tint.G &&
                   A.Tint.B == B.Tint.B &&
                   A.Tint.A == B.Tint.A;
        }

        if (MatA == RenderMaterialKind::SpriteEffect)
        {
            return A.Texture.Image.id == B.Texture.Image.id &&
                   A.Texture.View.id == B.Texture.View.id &&
                   A.HasEffectMask == B.HasEffectMask &&
                   A.EffectMask.Image.id == B.EffectMask.Image.id &&
                   A.EffectMask.View.id == B.EffectMask.View.id &&
                   A.TeamColor.R == B.TeamColor.R &&
                   A.TeamColor.G == B.TeamColor.G &&
                   A.TeamColor.B == B.TeamColor.B &&
                   A.TeamColor.A == B.TeamColor.A &&
                   std::fabs(A.Hallucination - B.Hallucination) <= 1e-4f &&
                   std::fabs(A.Cloaked - B.Cloaked) <= 1e-4f &&
                   std::fabs(A.Brighten - B.Brighten) <= 1e-4f &&
                   std::fabs(A.Shadow - B.Shadow) <= 1e-4f &&
                   std::fabs(A.WarpTexture - B.WarpTexture) <= 1e-4f &&
                   std::fabs(A.WarpScaleX - B.WarpScaleX) <= 1e-4f &&
                   std::fabs(A.WarpScaleY - B.WarpScaleY) <= 1e-4f;
        }

        return false;
    }

    static uint32_t CountFlushesForOrder(const std::vector<RenderCommand2D>& Commands)
    {
        if (Commands.empty())
            return 0;
        uint32_t Flushes = 1;
        int VertsInBatch = 4;
        for (size_t I = 1; I < Commands.size(); ++I)
        {
            if (!CanCoalesce(Commands[I - 1], Commands[I]) || VertsInBatch + 4 > 8192)
            {
                ++Flushes;
                VertsInBatch = 4;
            }
            else
            {
                VertsInBatch += 4;
            }
        }
        return Flushes;
    }

    RenderQueueStats ComputeStats()
    {
        RenderQueueStats Stats;
        Stats.TotalSubmitted = mTotalSubmitted;
        Stats.CulledCommands = mCulledCommands;
        Stats.RecordedCommands = static_cast<uint32_t>(mCommands.size());
        if (mCommands.empty())
            return Stats;

        // Compute unsorted flushes by ordering on SubmissionOrder
        std::vector<RenderCommand2D> Unsorted = mCommands;
        std::stable_sort(Unsorted.begin(), Unsorted.end(),
                         [](const RenderCommand2D& A, const RenderCommand2D& B)
                         {
                             return A.SubmissionOrder < B.SubmissionOrder;
                         });
        Stats.UnsortedFlushes = CountFlushesForOrder(Unsorted);

        Sort();
        Stats.SortedFlushes = CountFlushesForOrder(mCommands);
        Stats.StateChangesSaved = Stats.UnsortedFlushes > Stats.SortedFlushes
            ? (Stats.UnsortedFlushes - Stats.SortedFlushes)
            : 0u;
        return Stats;
    }

    void Playback(Draw2D& Target);

    void Submit(RenderCommand2D Cmd) {
        if (Cmd.SortKey == 0) {
            Cmd.SortKey = RenderCommand2D::ComputeSortKey(
                Cmd.Pass,
                Cmd.ElevationLayer,
                Cmd.WorldPosition.Y,
                Cmd.Material.MaterialID,
                0);
        }
        Cmd.SubmissionOrder = mNextSubmissionOrder++;
        mCommands.push_back(std::move(Cmd));
        mIsSorted = false;
    }

    void ExtractSceneTree(
        const SceneNode2D& Root,
        const Resource::ModernAssetRegistry* AssetRegistry = nullptr,
        uint8_t TeamColorID = 0)
    {
        if (!Root.IsVisible()) return;

        if (Root.GetRole() != SceneNodeRole::RootUnit && Root.GetImageID() != 0) {
            RenderCommand2D Cmd;
            Cmd.WorldPosition = Root.GetWorldPosition();
            Cmd.ImageID = Root.GetImageID();
            Cmd.FrameSet = Root.GetFrameSet();
            Cmd.Direction17 = Root.GetDirection17();
            Cmd.FlipHorizontal = Root.IsFlippedHorizontally();
            Cmd.ElevationLayer = Root.GetElevationSortLayer();
            Cmd.TeamColorID = TeamColorID;

            if (Root.GetRole() == SceneNodeRole::ShadowOverlay) {
                Cmd.Pass = (Cmd.ElevationLayer >= 12) ? RenderLayerPass::AirShadow : RenderLayerPass::GroundShadow;
                Cmd.Material.BlendMode = PBRBlendMode::AlphaBlendShadow;
            } else if (Root.GetRole() == SceneNodeRole::GlowOverlay || Root.GetRole() == SceneNodeRole::SocketAttachment2D) {
                Cmd.Pass = (Cmd.ElevationLayer >= 12) ? RenderLayerPass::AirOverlay : RenderLayerPass::GroundOverlay;
                Cmd.Material.BlendMode = PBRBlendMode::AdditiveEmissive;
            } else {
                Cmd.Pass = (Cmd.ElevationLayer >= 12) ? RenderLayerPass::AirUnit : RenderLayerPass::GroundUnit;
                Cmd.Material.BlendMode = PBRBlendMode::NormalDeferredLit;
            }

            Cmd.Material.ImageID = Cmd.ImageID;
            Cmd.Material.MaterialID = static_cast<uint16_t>(Cmd.ImageID & 0xFFFF);

            if (AssetRegistry) {
                if (const auto* Prefab = AssetRegistry->FindPBRPrefabByImageID(Cmd.ImageID)) {
                    Cmd.Material.ShaderName = Prefab->ShaderName;
                    Cmd.Material.BaseColorTexture = Prefab->BaseColorTexturePath;
                    Cmd.Material.NormalTeamAOTexture = Prefab->NormalTeamAOTexturePath;
                    Cmd.Material.SpecEmissiveTexture = Prefab->SpecEmissiveTexturePath;
                    Cmd.Material.HasNormalTeamAOTexture = !Prefab->NormalTeamAOTexturePath.empty();
                    Cmd.Material.HasSpecEmissiveTexture = !Prefab->SpecEmissiveTexturePath.empty();
                    if (Prefab->DrawMode.find("Glow") != std::string::npos ||
                        Prefab->DrawMode.find("Additive") != std::string::npos) {
                        Cmd.Material.BlendMode = PBRBlendMode::AdditiveEmissive;
                    } else if (Prefab->DrawMode.find("Shadow") != std::string::npos) {
                        Cmd.Material.BlendMode = PBRBlendMode::AlphaBlendShadow;
                    }
                }
            }

            Cmd.SortKey = RenderCommand2D::ComputeSortKey(
                Cmd.Pass,
                Cmd.ElevationLayer,
                Cmd.WorldPosition.Y,
                Cmd.Material.MaterialID,
                Root.GetZIndexOffset());
            Cmd.SubmissionOrder = mNextSubmissionOrder++;
            mCommands.push_back(std::move(Cmd));
            mIsSorted = false;
        }

        for (const auto& Child : Root.GetChildren()) {
            ExtractSceneTree(*Child, AssetRegistry, TeamColorID);
        }
    }

    void SortAndBatch() {
        Sort();
    }

    RenderBatchStats ComputeBatchStats() const {
        RenderBatchStats Stats;
        Stats.TotalCommands = mCommands.size();
        uint16_t LastMaterialID = UINT16_MAX;
        RenderLayerPass LastPass = RenderLayerPass::UI;

        for (size_t i = 0; i < mCommands.size(); ++i) {
            const auto& Cmd = mCommands[i];
            if (i == 0 || Cmd.Pass != LastPass || Cmd.Material.MaterialID != LastMaterialID) {
                ++Stats.DrawCalls;
                LastPass = Cmd.Pass;
                LastMaterialID = Cmd.Material.MaterialID;
            }
            if (Cmd.Material.BlendMode == PBRBlendMode::NormalDeferredLit) {
                ++Stats.PBRDeferredLitCommands;
            } else if (Cmd.Material.BlendMode == PBRBlendMode::AdditiveEmissive) {
                ++Stats.AdditiveGlowCommands;
            } else if (Cmd.Material.BlendMode == PBRBlendMode::AlphaBlendShadow) {
                ++Stats.ShadowCommands;
            }
        }
        return Stats;
    }

    const std::vector<RenderCommand2D>& GetCommands() const { return mCommands; }
    const std::string& GetTextPayload(uint32_t Index) const { return mTextPayloads[Index]; }
    size_t Size() const { return mCommands.size(); }
    bool Empty() const { return mCommands.empty(); }

private:
    bool IsCulled(float X, float Y, float W, float H) const
    {
        if (!mCullEnabled)
            return false;
        const float MinX = std::min(X, X + W);
        const float MaxX = std::max(X, X + W);
        const float MinY = std::min(Y, Y + H);
        const float MaxY = std::max(Y, Y + H);
        return MaxX < mCullMinX || MinX > mCullMaxX ||
               MaxY < mCullMinY || MinY > mCullMaxY;
    }

    std::vector<RenderCommand2D> mCommands;
    std::vector<std::string> mTextPayloads;
    SortContext2D mContext = {};
    uint32_t mNextSubmissionOrder = 0;
    uint32_t mTotalSubmitted = 0;
    uint32_t mCulledCommands = 0;
    float mCullMinX = -1e9f;
    float mCullMinY = -1e9f;
    float mCullMaxX = 1e9f;
    float mCullMaxY = 1e9f;
    bool mCullEnabled = false;
    bool mIsSorted = true;
};

class ScopedSortContext2D
{
public:
    ScopedSortContext2D(RenderQueue2D& Queue,
                        RenderLayer2D Layer,
                        RenderSubBand2D SubBand = RenderSubBand2D::RootSprite,
                        uint16_t DepthKey = 0x8000u)
        : mQueue(&Queue)
        , mPrev(Queue.GetSortContext())
    {
        Queue.SetSortContext(Layer, SubBand, DepthKey);
    }

    ScopedSortContext2D(RenderQueue2D* Queue,
                        RenderLayer2D Layer,
                        RenderSubBand2D SubBand = RenderSubBand2D::RootSprite,
                        uint16_t DepthKey = 0x8000u)
        : mQueue(Queue)
    {
        if (mQueue)
        {
            mPrev = mQueue->GetSortContext();
            mQueue->SetSortContext(Layer, SubBand, DepthKey);
        }
    }

    ~ScopedSortContext2D()
    {
        if (mQueue)
            mQueue->SetSortContext(mPrev.Layer, mPrev.SubBand, mPrev.DepthKey);
    }

private:
    RenderQueue2D* mQueue = nullptr;
    SortContext2D mPrev = {};
};

} // namespace Render
