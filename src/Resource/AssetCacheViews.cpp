#include "Resource/UIAssetCacheView.h"

#include "Resource/ResourceCache.h"

namespace Resource::Graphics
{

std::optional<std::vector<uint8_t>> ArchiveAssetSourceView::ReadFile(const std::string& Path) const
{
    return mCache ? mCache->ReadFile(Path) : std::nullopt;
}

std::optional<std::vector<uint8_t>> ArchiveAssetSourceView::ReadWithFallback(const std::string& Path) const
{
    return mCache ? mCache->ReadWithFallback(Path) : std::nullopt;
}

std::vector<std::string> ArchiveAssetSourceView::DebugBuildExtractedAssetCandidates(const std::string& Path) const
{
    return mCache ? mCache->DebugBuildExtractedAssetCandidates(Path) : std::vector<std::string>{};
}

Resource::GameDataLoader* DataAssetCacheView::GetDataLoader() const
{
    return mCache ? &mCache->GetDataLoader() : nullptr;
}

uint32_t SpriteAssetCacheView::ResolveImageID(int ChkUnitID) const
{
    return mCache ? mCache->ResolveImageID(ChkUnitID) : 0;
}

uint32_t SpriteAssetCacheView::ResolveConstructionImageID(int ChkUnitID) const
{
    return mCache ? mCache->ResolveConstructionImageID(ChkUnitID) : UINT32_MAX;
}

uint32_t SpriteAssetCacheView::ResolveSubunitImageID(int ChkUnitID) const
{
    return mCache ? mCache->ResolveSubunitImageID(ChkUnitID) : UINT32_MAX;
}

const std::vector<GpuImage>* SpriteAssetCacheView::GetSpriteFrames(uint32_t ImageID) const
{
    return mCache ? mCache->GetSpriteFrames(ImageID) : nullptr;
}

const std::vector<GpuImage>* SpriteAssetCacheView::GetDatImageFrames(uint32_t ImageID) const
{
    return mCache ? mCache->GetDatImageFrames(ImageID) : nullptr;
}

const std::vector<GpuImage>* SpriteAssetCacheView::GetSpriteOverlayFrames(uint32_t SpriteID) const
{
    return mCache ? mCache->GetSpriteOverlayFrames(SpriteID) : nullptr;
}

const std::vector<GpuImage>* SpriteAssetCacheView::GetSpriteFramesForChkUnit(int ChkUnitID) const
{
    if (!mCache || ChkUnitID < 0)
        return nullptr;
    return mCache->GetDatImageFrames(mCache->ResolveImageID(ChkUnitID));
}

bool TilesetAssetCacheView::SetTilesetPalette(const std::string& TilesetName) const
{
    return mCache ? mCache->SetTilesetPalette(TilesetName) : false;
}

const std::vector<GpuImage>* TilesetAssetCacheView::GetTilesetImages(const std::string& TilesetName) const
{
    return mCache ? mCache->GetTilesetImages(TilesetName) : nullptr;
}

const TilesetAtlas* TilesetAssetCacheView::GetTilesetAtlas(const std::string& TilesetName) const
{
    return mCache ? mCache->GetTilesetAtlas(TilesetName) : nullptr;
}

const TilesetAtlas* TilesetAssetCacheView::GetTilesetAtlasForMap(
    const std::string& TilesetName, const std::vector<uint16_t>* MegaTileIds) const
{
    return mCache ? mCache->GetTilesetAtlasForMap(TilesetName, MegaTileIds) : nullptr;
}

const std::vector<TilesetCreepFrame>* TilesetAssetCacheView::GetTilesetCreepFrames(
    const std::string& TilesetName) const
{
    return mCache ? mCache->GetTilesetCreepFrames(TilesetName) : nullptr;
}

const TilesetData* TilesetAssetCacheView::GetTilesetData(const std::string& TilesetName) const
{
    return mCache ? mCache->GetTilesetData(TilesetName) : nullptr;
}

const std::vector<GpuImage>* UIAssetCacheView::GetUIIcons() const
{
    return mCache ? mCache->GetUIIcons() : nullptr;
}

const std::vector<std::string>* UIAssetCacheView::GetUIIconUris() const
{
    return mCache ? mCache->GetUIIconUris() : nullptr;
}

const std::vector<GpuImage>* UIAssetCacheView::GetCommandIcons() const
{
    return mCache ? mCache->GetCommandIcons() : nullptr;
}

const std::vector<GpuImage>* UIAssetCacheView::GetCommandIconMasks() const
{
    return mCache ? mCache->GetCommandIconMasks() : nullptr;
}

const std::vector<std::string>* UIAssetCacheView::GetCommandIconUris() const
{
    return mCache ? mCache->GetCommandIconUris() : nullptr;
}

const std::vector<GpuImage>* UIAssetCacheView::GetCommandButtonSkins(CommandButtonSkinSet SkinSet) const
{
    return mCache ? mCache->GetCommandButtonSkins(SkinSet) : nullptr;
}

const std::vector<GpuImage>* UIAssetCacheView::GetWireframeFrames() const
{
    return mCache ? mCache->GetWireframeFrames() : nullptr;
}

bool UIAssetCacheView::EnsureWireframeFrame(int FrameIndex) const
{
    return mCache ? mCache->EnsureWireframeFrame(FrameIndex) : false;
}

const std::vector<GpuImage>* UIAssetCacheView::GetCursorFrames(const std::string& CursorName) const
{
    return mCache ? mCache->GetCursorFrames(CursorName) : nullptr;
}

const ImageOpaqueBounds* UIAssetCacheView::GetWireframeOpaqueBounds(int FrameIndex) const
{
    return mCache ? mCache->GetWireframeOpaqueBounds(FrameIndex) : nullptr;
}

const PortraitClip* UIAssetCacheView::GetPortraitClip(const std::string& PortraitKey) const
{
    return mCache ? mCache->GetPortraitClip(PortraitKey) : nullptr;
}

const PortraitFrame* UIAssetCacheView::GetPortraitFrame(const std::string& PortraitKey, double TimeSeconds) const
{
    return mCache ? mCache->GetPortraitFrame(PortraitKey, TimeSeconds) : nullptr;
}

GpuImage UIAssetCacheView::GetHudAggIcons() const
{
    return mCache ? mCache->GetHudAggIcons() : GpuImage{};
}

GpuImage UIAssetCacheView::GetHudTwireStrip() const
{
    return mCache ? mCache->GetHudTwireStrip() : GpuImage{};
}

GpuImage UIAssetCacheView::GetHudTunitStrip() const
{
    return mCache ? mCache->GetHudTunitStrip() : GpuImage{};
}

GpuImage UIAssetCacheView::GetHudHpBarEmptyStrip() const
{
    return mCache ? mCache->GetHudHpBarEmptyStrip() : GpuImage{};
}

GpuImage UIAssetCacheView::GetHudHpBarStrip() const
{
    return mCache ? mCache->GetHudHpBarStrip() : GpuImage{};
}

GpuImage UIAssetCacheView::GetUIConsoleLeft() const
{
    return mCache ? mCache->GetUIConsoleLeft() : GpuImage{};
}

GpuImage UIAssetCacheView::GetUIConsoleCenter() const
{
    return mCache ? mCache->GetUIConsoleCenter() : GpuImage{};
}

GpuImage UIAssetCacheView::GetUIConsoleRight() const
{
    return mCache ? mCache->GetUIConsoleRight() : GpuImage{};
}

GpuImage UIAssetCacheView::GetUIConsolePortraitFrame() const
{
    return mCache ? mCache->GetUIConsolePortraitFrame() : GpuImage{};
}

GpuImage UIAssetCacheView::GetUIConsoleApm() const
{
    return mCache ? mCache->GetUIConsoleApm() : GpuImage{};
}

GpuImage UIAssetCacheView::GetUIConsoleTime() const
{
    return mCache ? mCache->GetUIConsoleTime() : GpuImage{};
}

GpuImage UIAssetCacheView::GetUIConsole() const
{
    return mCache ? mCache->GetUIConsole() : GpuImage{};
}

GpuImage UIAssetCacheView::GetUIConsoleOver() const
{
    return mCache ? mCache->GetUIConsoleOver() : GpuImage{};
}

const PcxRegions* UIAssetCacheView::GetUIRegions() const
{
    return mCache ? &mCache->GetUIRegions() : nullptr;
}

} // namespace Resource::Graphics
