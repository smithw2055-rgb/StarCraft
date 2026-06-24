#include "Render/SpriteAtlas.h"
#include "Core/Utils/Logger.h"

namespace Render
{

bool SpriteAtlas::LoadPage(const std::string& Name, Texture2D Tex)
{
    if (Tex.id <= 0)
    {
        Core::Utils::Logger::Warning("SpriteAtlas", "Cannot load page with invalid texture: " + Name);
        return false;
    }

    AtlasPage Page;
    Page.Texture = Tex;
    Page.Name = Name;
    Page.Width = Tex.width;
    Page.Height = Tex.height;
    mPages[Name] = Page;

    Core::Utils::Logger::Info("SpriteAtlas",
        "Loaded page: " + Name + " (" + std::to_string(Tex.width) + "x" +
        std::to_string(Tex.height) + ")");
    return true;
}

bool SpriteAtlas::AddSprite(uint32_t SpriteID, const std::vector<AtlasFrame>& Frames, bool Loop)
{
    if (Frames.empty())
    {
        Core::Utils::Logger::Warning("SpriteAtlas",
            "Cannot add sprite with no frames: ID=" + std::to_string(SpriteID));
        return false;
    }

    SpriteDef Def;
    Def.ID = SpriteID;
    Def.Frames = Frames;
    Def.FrameCount = (int)Frames.size();
    Def.Loop = Loop;

    Def.TotalDuration = 0.0f;
    for (auto& F : Frames)
        Def.TotalDuration += F.Duration;

    mSprites[SpriteID] = Def;
    return true;
}

const SpriteDef* SpriteAtlas::GetSprite(uint32_t SpriteID) const
{
    auto It = mSprites.find(SpriteID);
    if (It != mSprites.end())
        return &It->second;
    return nullptr;
}

const AtlasFrame* SpriteAtlas::GetFrame(uint32_t SpriteID, int FrameIndex) const
{
    auto* Def = GetSprite(SpriteID);
    if (!Def || FrameIndex < 0 || FrameIndex >= Def->FrameCount)
        return nullptr;
    return &Def->Frames[FrameIndex];
}

int SpriteAtlas::GetFrameCount(uint32_t SpriteID) const
{
    auto* Def = GetSprite(SpriteID);
    return Def ? Def->FrameCount : 0;
}

const AtlasPage* SpriteAtlas::GetPage(const std::string& Name) const
{
    auto It = mPages.find(Name);
    if (It != mPages.end())
        return &It->second;
    return nullptr;
}

void SpriteAtlas::UnloadAll()
{
    for (auto& [Name, Page] : mPages)
    {
        if (Page.Texture.id > 0)
            UnloadTexture(Page.Texture);
    }
    mPages.clear();
    mSprites.clear();

    Core::Utils::Logger::Info("SpriteAtlas", "Unloaded all pages and sprites");
}

} // namespace Render
