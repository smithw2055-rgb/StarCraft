#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

#include "raylib.h"

namespace Render
{

struct AtlasFrame
{
    Rectangle SourceRect = {};
    float Duration = 0.1f;
    int FrameIndex = 0;
};

struct SpriteDef
{
    uint32_t ID = 0;
    std::vector<AtlasFrame> Frames;
    int FrameCount = 0;
    float TotalDuration = 0.0f;
    bool Loop = true;
};

struct AtlasPage
{
    Texture2D Texture = {};
    std::string Name;
    int Width = 0;
    int Height = 0;
};

class SpriteAtlas
{
public:
    bool LoadPage(const std::string& Name, Texture2D Tex);
    bool AddSprite(uint32_t SpriteID, const std::vector<AtlasFrame>& Frames, bool Loop = true);

    const SpriteDef* GetSprite(uint32_t SpriteID) const;
    const AtlasFrame* GetFrame(uint32_t SpriteID, int FrameIndex) const;
    int GetFrameCount(uint32_t SpriteID) const;

    const AtlasPage* GetPage(const std::string& Name) const;

    void UnloadAll();

    static SpriteAtlas& GetInstance()
    {
        static SpriteAtlas Instance;
        return Instance;
    }

private:
    SpriteAtlas() = default;

    std::unordered_map<uint32_t, SpriteDef> mSprites;
    std::unordered_map<std::string, AtlasPage> mPages;
};

} // namespace Render
