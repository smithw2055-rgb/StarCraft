#include "Render/TextureManager.h"
#include "Core/Utils/Logger.h"
#include <cstdint>

namespace Render
{

Texture2D LoadTextureFromImage(const Image& Img)
{
    if (Img.data == nullptr)
    {
        Core::Utils::Logger::Warning("TextureManager", "Null image data");
        return {};
    }

    Texture2D Tex = ::LoadTextureFromImage(Img);
    Core::Utils::Logger::Info("TextureManager",
        "Loaded texture: " + std::to_string(Tex.width) + "x" + std::to_string(Tex.height) +
        " format=" + std::to_string(Tex.format) +
        " mipmaps=" + std::to_string(Tex.mipmaps));
    
    // Sample some pixels
    if (Tex.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 && Tex.id > 0)
    {
        Image CheckImg = ::LoadImageFromTexture(Tex);
        if (CheckImg.data)
        {
            uint8_t* Pixels = (uint8_t*)CheckImg.data;
            int NonZero = 0;
            for (int P = 0; P < CheckImg.width * CheckImg.height * 4 && P < 200; P += 4)
            {
                if (Pixels[P] != 0 || Pixels[P+1] != 0 || Pixels[P+2] != 0 || Pixels[P+3] != 0)
                    NonZero++;
            }
            Core::Utils::Logger::Info("TextureManager",
                "  Sampled " + std::to_string(NonZero) + "/50 non-transparent pixels");
            ::UnloadImage(CheckImg);
        }
    }
    
    return Tex;
}

} // namespace Render
