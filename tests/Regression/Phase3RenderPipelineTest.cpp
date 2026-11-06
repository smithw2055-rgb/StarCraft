#include "Client/EntityRenderer.h"
#include "Core/ECS/EntityManager.h"
#include "Game/Components.h"
#include "Render/Draw2D.h"
#include "Render/RenderQueue2D.h"
#include "Render/RenderServer2D.h"
#include "Resource/GameDataLoader.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

// ============================================================================
// Headless Draw2D & AssetView stubs for end-to-end EntityRenderer verification
// ============================================================================
namespace Render
{
    Draw2D g_Draw2D;

    void Draw2D::Flush()
    {
        if (mVerticesInBatch == 0)
            return;
        ++mFlushCountInFrame;
        mVerticesInBatch = 0;
        mIndicesInBatch = 0;
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
    }

    void Draw2D::SetHealthBarParams(float Fraction, Color Fill)
    {
        Fraction = std::clamp(Fraction, 0.0f, 1.0f);
        auto ToFloat = [](uint8_t V) { return static_cast<float>(V) / 255.0f; };
        HealthBarShaderParams NewParams = {};
        NewParams.SolidColor[0] = ToFloat(Fill.R);
        NewParams.SolidColor[1] = ToFloat(Fill.G);
        NewParams.SolidColor[2] = ToFloat(Fill.B);
        NewParams.SolidColor[3] = ToFloat(Fill.A);
        NewParams.MultiplyColor[0] = Fraction;
        NewParams.MultiplyColor[1] = Fraction;
        NewParams.MultiplyColor[3] = 1.0f;
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
        auto ToFloat = [](uint8_t V) { return static_cast<float>(V) / 255.0f; };
        SpriteEffectShaderParams NewParams = {};
        NewParams.TeamColor[0] = ToFloat(TeamColor.R);
        NewParams.TeamColor[1] = ToFloat(TeamColor.G);
        NewParams.TeamColor[2] = ToFloat(TeamColor.B);
        NewParams.TeamColor[3] = ToFloat(TeamColor.A);
        NewParams.EffectData[0] = Hallucination;
        NewParams.EffectData[1] = Cloaked;
        NewParams.EffectData[2] = Brighten;
        NewParams.EffectData[3] = UseTeamColor ? 1.0f : 0.0f;
        NewParams.EffectMode[0] = Shadow;
        NewParams.EffectMode[1] = WarpTexture;
        NewParams.EffectMode[2] = WarpScaleX;
        NewParams.EffectMode[3] = WarpScaleY;
        if (mCurrentMaterial != BatchMaterial::SpriteEffect ||
            std::memcmp(&mSpriteEffectParams, &NewParams, sizeof(NewParams)) != 0)
        {
            Flush();
            mSpriteEffectParams = NewParams;
        }
    }

    void Draw2D::DrawSprite(const GpuTextureHandle& Texture,
                            float X, float Y, float W, float H,
                            float U0, float V0, float U1, float V1,
                            Color Tint, bool FlipX)
    {
        if (!Texture.Valid())
            return;
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
        mVerticesInBatch += 4;
        mIndicesInBatch += 6;
    }

    void Draw2D::DrawSpriteEffect(const GpuTextureHandle& Texture,
                                  float X, float Y, float W, float H,
                                  float U0, float V0, float U1, float V1,
                                  Color Tint, bool FlipX,
                                  float Hallucination, float Cloaked, float Brighten,
                                  float Shadow, float WarpTexture,
                                  float WarpScaleX, float WarpScaleY,
                                  Color TeamColor,
                                  const GpuTextureHandle* EffectMask)
    {
        if (!Texture.Valid())
            return;
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
        SetSpriteEffectParams(Hallucination, Cloaked, Brighten, Shadow,
                              UseWarpTexture ? 1.0f : 0.0f,
                              WarpScaleX, WarpScaleY, TeamColor, UseTeamColor);
        SetPipeline(true, BatchMaterial::SpriteEffect);
        mVerticesInBatch += 4;
        mIndicesInBatch += 6;
    }

    void Draw2D::DrawRect(float X, float Y, float W, float H, Color C)
    {
        if (mActiveQueue)
        {
            mActiveQueue->SubmitRect(X, Y, W, H, C);
            return;
        }
        if (mCurrentTexture.Image.id != mWhiteTexture.Image.id ||
            mCurrentMaterial != BatchMaterial::Textured)
        {
            Flush();
            mCurrentTexture = mWhiteTexture;
        }
        SetPipeline(true, BatchMaterial::Textured);
        mVerticesInBatch += 4;
        mIndicesInBatch += 6;
    }

    void Draw2D::DrawRectLines(float X, float Y, float W, float H, Color C, float Thickness)
    {
        DrawRect(X, Y, W, Thickness, C);
        DrawRect(X, Y + H - Thickness, W, Thickness, C);
        DrawRect(X, Y, Thickness, H, C);
        DrawRect(X + W - Thickness, Y, Thickness, H, C);
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
        mVerticesInBatch += 4;
        mIndicesInBatch += 6;
    }

    void Draw2D::DrawText(const std::string& Text, float X, float Y, float Size, Color C)
    {
        if (Text.empty())
            return;
        if (mActiveQueue)
        {
            mActiveQueue->SubmitText(Text, X, Y, Size, C);
            return;
        }
        SetPipeline(true, BatchMaterial::Textured);
        mVerticesInBatch += 4;
        mIndicesInBatch += 6;
    }
} // namespace Render

namespace Resource::Graphics
{
    Resource::GameDataLoader* DataAssetCacheView::GetDataLoader() const { return nullptr; }
    const std::vector<GpuImage>* SpriteAssetCacheView::GetDatImageFrames(uint32_t) const { return nullptr; }
    const std::vector<GpuImage>* SpriteAssetCacheView::GetSpriteOverlayFrames(uint32_t) const { return nullptr; }
} // namespace Resource::Graphics

namespace Resource
{
    const UnitEntry* GameDataLoader::FindUnitByID(uint32_t) const { return nullptr; }
    const ImageEntry* GameDataLoader::FindImageByID(uint32_t) const { return nullptr; }
    const SpriteEntry* GameDataLoader::FindSpriteByID(uint32_t) const { return nullptr; }
    std::string GameDataLoader::ResolveImageGrpPath(uint32_t) const { return {}; }
    uint32_t GameDataLoader::ResolveUnitSpriteID(uint32_t) const { return UINT32_MAX; }
} // namespace Resource

namespace
{

Render::GpuTextureHandle MakeFakeTexture(uint32_t ImageId, uint32_t ViewId)
{
    Render::GpuTextureHandle H;
    H.Image.id = ImageId;
    H.View.id = ViewId;
    return H;
}

void TestSortKeyBitPackingAndHierarchy()
{
    using namespace Render;

    const uint64_t Key = SortKey2D::Pack(
        static_cast<uint8_t>(RenderLayer2D::GroundEntities),
        static_cast<uint8_t>(RenderSubBand2D::RootSprite),
        0x4321u,
        RenderMaterialKind::SpriteEffect,
        0xABCDu,
        0x0789u);

    assert(SortKey2D::DecodeLayer(Key) == static_cast<uint8_t>(RenderLayer2D::GroundEntities));
    assert(SortKey2D::DecodeSubBand(Key) == static_cast<uint8_t>(RenderSubBand2D::RootSprite));
    assert(SortKey2D::DecodeDepthKey(Key) == 0x4321u);
    assert(SortKey2D::DecodeMaterial(Key) == RenderMaterialKind::SpriteEffect);
    assert(SortKey2D::DecodeTextureStateKey(Key) == 0xABCDu);
    assert(SortKey2D::DecodeSequence(Key) == 0x0789u);

    const uint16_t D0 = SortKey2D::EncodeDepthKey(100.0f, 0);
    const uint16_t D1 = SortKey2D::EncodeDepthKey(100.5f, 0);
    const uint16_t D2 = SortKey2D::EncodeDepthKey(250.0f, 0);
    const uint16_t D3 = SortKey2D::EncodeDepthKey(250.0f, 1);
    assert(D0 < D1);
    assert(D1 < D2);
    assert(D2 < D3);

    const uint64_t GroundKey = SortKey2D::Pack(
        static_cast<uint8_t>(RenderLayer2D::GroundEntities), 255, 65535,
        RenderMaterialKind::HealthBar, 65535, 4095);
    const uint64_t AirKey = SortKey2D::Pack(
        static_cast<uint8_t>(RenderLayer2D::AirEntities), 0, 0,
        RenderMaterialKind::Textured, 0, 0);
    assert(GroundKey < AirKey);

    const uint64_t ShadowSubKey = SortKey2D::Pack(
        static_cast<uint8_t>(RenderLayer2D::GroundEntities),
        static_cast<uint8_t>(RenderSubBand2D::Shadow),
        0x5000u, RenderMaterialKind::SpriteEffect, 0xFFFFu, 4095);
    const uint64_t RootSubKey = SortKey2D::Pack(
        static_cast<uint8_t>(RenderLayer2D::GroundEntities),
        static_cast<uint8_t>(RenderSubBand2D::RootSprite),
        0x1000u, RenderMaterialKind::Textured, 0u, 0);
    assert(ShadowSubKey < RootSubKey);
}

void TestStatusBarCoalescingAcrossSelectedUnits()
{
    using namespace Render;

    RenderQueue2D Queue;
    Queue.DisableCulling();

    const Color GreenHp(78, 220, 78, 255);
    const Color BlueShield(64, 112, 255, 255);
    const Color BlackBorder(0, 0, 0, 235);

    for (int UnitIdx = 0; UnitIdx < 24; ++UnitIdx)
    {
        const float X = static_cast<float>(UnitIdx * 32);
        const float Y = static_cast<float>(UnitIdx * 16);

        Queue.SetSortContext(RenderLayer2D::StatusBars, RenderSubBand2D::StatusBarBorder, 0u);
        Queue.SubmitRect(X - 1.0f, Y - 1.0f, 34.0f, 7.0f, BlackBorder);
        Queue.SetSortContext(RenderLayer2D::StatusBars, RenderSubBand2D::StatusBarFill, 0u);
        Queue.SubmitHealthBar(X, Y, 32.0f, 5.0f, 1.0f, GreenHp);

        Queue.SetSortContext(RenderLayer2D::StatusBars, RenderSubBand2D::StatusBarBorder, 0u);
        Queue.SubmitRect(X - 1.0f, Y + 5.0f, 34.0f, 7.0f, BlackBorder);
        Queue.SetSortContext(RenderLayer2D::StatusBars, RenderSubBand2D::StatusBarFill, 0u);
        Queue.SubmitHealthBar(X, Y + 6.0f, 32.0f, 5.0f, 1.0f, BlueShield);
    }

    const RenderQueueStats Stats = Queue.ComputeStats();
    assert(Stats.RecordedCommands == 96u);
    assert(Stats.UnsortedFlushes == 96u);
    assert(Stats.SortedFlushes == 3u);
    assert(Stats.StateChangesSaved == 93u);
}

void TestSpriteAndTeamColorBatchingAndLayerOrder()
{
    using namespace Render;

    RenderQueue2D Queue;
    Queue.DisableCulling();

    const GpuTextureHandle MarineTex = MakeFakeTexture(10, 100);
    const GpuTextureHandle ZerglingTex = MakeFakeTexture(20, 200);
    const GpuTextureHandle MarineMask = MakeFakeTexture(11, 101);

    Queue.SetSortContext(RenderLayer2D::StatusBars, RenderSubBand2D::StatusBarBorder, 0u);
    Queue.SubmitRect(0.0f, 0.0f, 20.0f, 4.0f, Color(0, 0, 0, 255));

    Queue.SetSortContext(RenderLayer2D::AirEntities, RenderSubBand2D::RootSprite,
                         SortKey2D::EncodeDepthKey(300.0f, 0));
    Queue.SubmitSprite(MarineTex, 100.0f, 300.0f, 32.0f, 32.0f);

    const uint16_t GroundDepth = SortKey2D::EncodeDepthKey(200.0f, 0);
    Queue.SetSortContext(RenderLayer2D::GroundEntities, RenderSubBand2D::RootSprite, GroundDepth);
    for (int I = 0; I < 10; ++I)
    {
        Queue.SubmitSpriteEffect(
            MarineTex, static_cast<float>(I * 16), 200.0f, 32.0f, 32.0f,
            0.0f, 0.0f, 1.0f, 1.0f, Colors::White(), false,
            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f,
            Color(255, 0, 0, 255), &MarineMask);
        Queue.SubmitSprite(
            ZerglingTex, static_cast<float>(I * 16 + 8), 200.0f, 32.0f, 32.0f);
    }

    Queue.SetSortContext(RenderLayer2D::SelectionOverlay, RenderSubBand2D::RootSprite, 0u);
    Queue.SubmitRect(50.0f, 50.0f, 24.0f, 12.0f, Color(70, 255, 86, 255));

    const RenderQueueStats Stats = Queue.ComputeStats();
    assert(Stats.RecordedCommands == 23u);
    assert(Stats.SortedFlushes < Stats.UnsortedFlushes);

    const auto& Cmds = Queue.GetCommands();
    assert(SortKey2D::DecodeLayer(Cmds.front().SortKey) ==
           static_cast<uint8_t>(RenderLayer2D::SelectionOverlay));
    assert(SortKey2D::DecodeLayer(Cmds.back().SortKey) ==
           static_cast<uint8_t>(RenderLayer2D::StatusBars));

    for (size_t I = 1; I < Cmds.size(); ++I)
    {
        assert(Cmds[I - 1].SortKey <= Cmds[I].SortKey);
        if (Cmds[I - 1].SortKey == Cmds[I].SortKey)
            assert(Cmds[I - 1].SubmissionOrder < Cmds[I].SubmissionOrder);
    }
}

void TestRenderServerFrustumCullingAndScopedContext()
{
    using namespace Render;

    RenderServer2D Server;
    Server.BeginWorldFrame(500.0f, 500.0f, 800.0f, 600.0f, 1.0f, 100.0f);

    RenderQueue2D& WorldQ = Server.GetWorldQueue();
    WorldQ.SetSortContext(RenderLayer2D::GroundEntities, RenderSubBand2D::RootSprite, 100u);

    {
        ScopedSortContext2D Scope(WorldQ, RenderLayer2D::SelectionOverlay, RenderSubBand2D::UnderNode, 5u);
        assert(WorldQ.GetSortContext().Layer == static_cast<uint8_t>(RenderLayer2D::SelectionOverlay));
        assert(WorldQ.GetSortContext().SubBand == static_cast<uint8_t>(RenderSubBand2D::UnderNode));
        assert(WorldQ.GetSortContext().DepthKey == 5u);
        WorldQ.SubmitRect(480.0f, 480.0f, 32.0f, 32.0f, Colors::White());
        WorldQ.SubmitRect(-500.0f, -500.0f, 32.0f, 32.0f, Colors::White());
        WorldQ.SubmitRect(2000.0f, 2000.0f, 32.0f, 32.0f, Colors::White());
    }

    assert(WorldQ.GetSortContext().Layer == static_cast<uint8_t>(RenderLayer2D::GroundEntities));
    assert(WorldQ.GetSortContext().SubBand == static_cast<uint8_t>(RenderSubBand2D::RootSprite));
    assert(WorldQ.GetSortContext().DepthKey == 100u);

    const RenderQueueStats Stats = WorldQ.ComputeStats();
    assert(Stats.TotalSubmitted == 3u);
    assert(Stats.CulledCommands == 2u);
    assert(Stats.RecordedCommands == 1u);
}

void TestEntityRendererEndToEndWithECS()
{
    Core::ECS::EntityManager EM;
    std::vector<int> SelectedIds;

    // Create 12 selected Protoss Zealots with HP + Shields
    for (int I = 0; I < 12; ++I)
    {
        const int E = EM.CreateEntity();
        auto& T = EM.AddComponent<Game::Components::TransformComponent>(E);
        T.Position = {400.0f + static_cast<float>(I * 20), 400.0f + static_cast<float>((I % 4) * 16)}; T.PrevPosition = T.Position; T.HasPrevPosition = true;
        auto& R = EM.AddComponent<Game::Components::RenderComponent>(E);
        R.Visible = true;
        R.ImageID = 213u;
        auto& U = EM.AddComponent<Game::Components::UnitComponent>(E);
        U.ChkUnitID = 65; // Zealot
        U.UnitRace = Game::Components::Race::Protoss;
        U.HitPoints = 100;
        U.MaxHitPoints = 100;
        U.ShieldPoints = 60;
        U.MaxShieldPoints = 60;
        auto& P = EM.AddComponent<Game::Components::PlayerComponent>(E);
        P.PlayerID = 1;
        SelectedIds.push_back(E);
    }

    // Create 1 selected Protoss Pylon (ChkUnitID 156) to emit GroundPowerField
    {
        const int Pylon = EM.CreateEntity();
        auto& T = EM.AddComponent<Game::Components::TransformComponent>(Pylon);
        T.Position = {500.0f, 450.0f}; T.PrevPosition = T.Position; T.HasPrevPosition = true;
        auto& R = EM.AddComponent<Game::Components::RenderComponent>(Pylon);
        R.Visible = true;
        auto& U = EM.AddComponent<Game::Components::UnitComponent>(Pylon);
        U.ChkUnitID = 156;
        U.UnitRace = Game::Components::Race::Protoss;
        U.HitPoints = 300;
        U.MaxHitPoints = 300;
        U.ShieldPoints = 300;
        U.MaxShieldPoints = 300;
        EM.AddComponent<Game::Components::BuildingComponent>(Pylon);
        SelectedIds.push_back(Pylon);
    }

    Game::Client::EntityRenderer Renderer;
    Renderer.SetCamera(500.0f, 450.0f, 1280.0f, 720.0f, 1.0f);

    // 1. Run with RenderQueue2D enabled (default)
    Render::g_Draw2D.ResetFlushCountInFrame();
    Renderer.SetUseRenderQueue(true);
    Renderer.Render(EM, SelectedIds);

    const auto& FrameStats = Renderer.GetLastRenderStats();
    assert(FrameStats.FrameIndex == 1u);
    assert(FrameStats.WorldStats.RecordedCommands > 200u);
    // Verify that sorting saved at least 24 state changes across the 13 selected units' HP + Shield bars!
    assert(FrameStats.WorldStats.StateChangesSaved >= 24u);
    assert(FrameStats.WorldStats.SortedFlushes < FrameStats.WorldStats.UnsortedFlushes);
}

} // namespace

int main()
{
    TestSortKeyBitPackingAndHierarchy();
    TestStatusBarCoalescingAcrossSelectedUnits();
    TestSpriteAndTeamColorBatchingAndLayerOrder();
    TestRenderServerFrustumCullingAndScopedContext();
    TestEntityRendererEndToEndWithECS();

    std::cout << "[Phase3RenderPipelineTest] All 64-bit SortKey, RenderQueue2D & EntityRenderer ECS tests passed.\n";
    return 0;
}
