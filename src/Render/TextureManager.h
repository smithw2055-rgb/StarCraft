#pragma once

#include <string>
#include <unordered_map>

#include "raylib.h"

namespace Render
{

class TextureManager
{
public:
    int LoadTexture(const std::string& FilePath);
    Texture2D GetTexture(int ID) const;
    void UnloadAll();

    static TextureManager& GetInstance()
    {
        static TextureManager Instance;
        return Instance;
    }

private:
    TextureManager() = default;
    std::unordered_map<int, Texture2D> mTextures;
    std::unordered_map<std::string, int> mPathToID;
    int mNextID = 1;
};

} // namespace Render
