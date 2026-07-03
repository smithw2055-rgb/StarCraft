#include "Resource/ResourceCache.h"

#include "Core/Utils/Logger.h"

#include "Resource/UIAssetImageHelpers.h"



namespace Resource::Graphics
{


bool ResourceCache::LoadUIGraphics()
{
    std::optional<ColorPalette> UiPalette;
    if (auto TIcon = ReadUIFileWithUpperFallback("sprites\\cmdbtns\\ticon.pcx"))
        UiPalette = LoadPcxPalette(*TIcon);

    const ColorPalette* UiPalettePtr = UiPalette ? &*UiPalette : nullptr;

    GrpDecodeOptions CommandButtonOptions = {};
    CommandButtonOptions.PaletteOverride = UiPalettePtr;
    CommandButtonOptions.TransparentIndexZero = true;

    GrpDecodeOptions CursorOptions = CommandButtonOptions;
    CursorOptions.TransparentBlack = true;
    CursorOptions.TransparentRgb = RawColor(0, 0, 0, 0);

    LoadUIIconGraphics();
    LoadCommandButtonGraphics(CommandButtonOptions);
    LoadWireframeGraphics();
    LoadHudChromeGraphics();
    LoadBlinkGraphics();

    if (mSources.UseExtractedAssets)
        LoadHdConsoleGraphics();
    else
        LoadClassicConsoleGraphics();

    LoadCursorGraphics(CursorOptions);

    Core::Utils::Logger::Info("ResourceCache",
        "UI graphics: " + std::to_string(mUI.Icons.size()) + " icons, " +
        std::to_string(mUI.CommandIcons.size()) + " command icons, " +
        std::to_string(mUI.CommandButtonSkins[static_cast<size_t>(CommandButtonSkinSet::Terran)].size()) +
        " terran button skins, " +
        std::to_string(mUI.CursorFrames.size()) + " cursors" +
        (mUI.Blink.Valid() ? ", blink" : "") +
        (mUI.Console.Valid() ? ", console" : "") +
        (mUI.ConsoleOver.Valid() ? ", conover" : ""));
    return true;
}

} // namespace Resource::Graphics































