#include "Resource/ResourceCache.h"
#include "Resource/MpqArchive.h"
#include "Resource/CascArchive.h"
#include "Resource/FileSystemArchive.h"
#include "Resource/Cv5Decoder.h"
#include "Resource/Vx4Decoder.h"
#include "Resource/Vf4Decoder.h"
#include "Resource/TblDecoder.h"
#include "Resource/PcxDecoder.h"
#include "Core/Utils/Logger.h"

#include "raylib.h"

#include <cstring>

namespace Resource
{

namespace
{

// Raylib adapter: convert RawImage -> raylib::Image (caller must UnloadImage).
Image ToRaylibImage(const RawImage& Img)
{
    Image Out = {};
    Out.width = Img.Width;
    Out.height = Img.Height;
    Out.mipmaps = 1;
    Out.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    Out.data = malloc(Img.Rgba.size() ? Img.Rgba.size() : 1);
    if (!Img.Rgba.empty())
        memcpy(Out.data, Img.Rgba.data(), Img.Rgba.size());
    return Out;
}

// Raylib adapter: load raylib textures from raw GRP frames.
std::optional<std::vector<Texture2D>> DecodeGrpToRaylibTextures(
    const std::vector<uint8_t>& GrpData, const RawPalette& Pal)
{
    auto ImgsOpt = GrpDecoder::DecodeToRawImages(GrpData, Pal);
    if (!ImgsOpt) return std::nullopt;

    std::vector<Texture2D> Textures;
    Textures.reserve(ImgsOpt->size());
    for (auto& Img : *ImgsOpt)
    {
        Image RlImg = ToRaylibImage(Img);
        Texture2D Tex = LoadTextureFromImage(RlImg);
        UnloadImage(RlImg);
        Textures.push_back(Tex);
    }
    return Textures;
}

} // namespace

ResourceCache::ResourceCache()
{
}

ResourceCache::~ResourceCache()
{
    for (auto& [ID, Textures] : mTextureCache)
    {
        for (auto& Tex : Textures)
        {
            if (Tex.id > 0)
                UnloadTexture(Tex);
        }
    }
    mTextureCache.clear();
}

bool ResourceCache::OpenCascStorage(const std::string& StoragePath,
                                     std::unique_ptr<IArchiveReader>& OutArchive)
{
    auto Reader = std::make_unique<CascArchive>();
    if (Reader->Open(StoragePath))
    {
        OutArchive = std::move(Reader);
        return true;
    }
    return false;
}

bool ResourceCache::OpenMpqArchives(const std::string& InstallPath)
{
    // Try opening MPQ archives with priority: Patch_rt > BrooDat > StarDat
    auto TryOpen = [&](const std::string& Name, auto& Out) -> bool
    {
        auto Reader = std::make_unique<MpqArchive>();
        std::string FullPath = InstallPath + "/" + Name;
        if (Reader->Open(FullPath))
        {
            Out = std::move(Reader);
            return true;
        }
        FullPath = InstallPath + "\\" + Name;
        if (Reader->Open(FullPath))
        {
            Out = std::move(Reader);
            return true;
        }
        return false;
    };

    TryOpen("Patch_rt.mpq", mArchive3);
    TryOpen("BrooDat.mpq", mArchive);
    TryOpen("StarDat.mpq", mArchive2);

    // Attach (listfile) to each archive for filename resolution
    std::string LfPath = InstallPath + "/(listfile)";
    auto AttachLf = [&](auto& Arc) {
        if (Arc) ((Resource::MpqArchive*)Arc.get())->AddListFile(LfPath);
    };
    AttachLf(mArchive3);
    AttachLf(mArchive);
    AttachLf(mArchive2);

    if (!mArchive && mArchive2)
        mArchive = std::move(mArchive2);

    if (!mArchive)
        return false;

    Core::Utils::Logger::Info("ResourceCache", "Opened game MPQ");
    return true;
}

bool ResourceCache::Initialize(const std::string& GameInstallPath)
{
    Core::Utils::Logger::Info("ResourceCache",
        "Initializing with: " + GameInstallPath);

    // ── Check for extracted assets first ────────────────
    std::string AssetsPath = GameInstallPath + "/assets";
    if (OpenAssetsDirectory(AssetsPath))
    {
        mUseAssets = true;
        mIsCasc = false;
        Core::Utils::Logger::Info("ResourceCache",
            "Using extracted assets from: " + AssetsPath);
    }
    else
    {
        // ── Detect CASC or MPQ installation ────────────────
        GameInstallInfo Info = GameDetector::CheckPath(GameInstallPath);

        if (Info.Type == ArchiveType::Casc)
        {
            if (!OpenCascStorage(Info.CascDataPath, mArchive))
            {
                Core::Utils::Logger::Warning("ResourceCache",
                    "Failed to open CASC storage at: " + Info.CascDataPath);
                return false;
            }
            mIsCasc = true;
            Core::Utils::Logger::Info("ResourceCache",
                "Opened CASC storage: " + Info.CascDataPath);
        }
        else
        {
            if (!OpenMpqArchives(GameInstallPath))
            {
                Core::Utils::Logger::Warning("ResourceCache",
                    "No game data at: " + GameInstallPath);
                return false;
            }
            mIsCasc = false;
        }
    }

    // ── Load data tables ───────────────────────────────
    mDataLoader = GameDataLoader();

    if (mIsCasc)
    {
        // CASC: simple direct reads, no case-variant fallbacks needed
        auto LoadTable = [&](const std::string& Name, auto LoadFn) {
            auto Data = mArchive->ReadFile(Name);
            if (Data) LoadFn(*Data);
            else Core::Utils::Logger::Warning("ResourceCache", "Cannot load: " + Name);
        };

        LoadTable("units.dat", [&](auto& D) { mDataLoader.LoadUnitDataRaw(D); });
        LoadTable("weapons.dat", [&](auto& D) { mDataLoader.LoadWeaponDataRaw(D); });
        LoadTable("flingy.dat", [&](auto& D) { mDataLoader.LoadFlingyDataRaw(D); });
        LoadTable("sprites.dat", [&](auto& D) { mDataLoader.LoadSpriteDataRaw(D); });
        LoadTable("images.dat", [&](auto& D) { mDataLoader.LoadImageDataRaw(D); });
        LoadTable("upgrades.dat", [&](auto& D) { mDataLoader.LoadUpgradeDataRaw(D); });
        LoadTable("techdata.dat", [&](auto& D) { mDataLoader.LoadTechDataRaw(D); });
        LoadTable("images.tbl", [&](auto& D) { mDataLoader.LoadImageTblDataRaw(D); });
        mDataLoader.RebuildImageMappingFromTbl();

        mPalette = mDataLoader.LoadUnitPalette(*mArchive);
    }
    else
    {
        // MPQ: try multiple case variants + arr/ prefix fallback
        auto TryPaths = [&](const std::string& Base, auto& Archive) -> std::optional<std::vector<uint8_t>>
        {
            auto D = Archive->ReadFile(Base);
            if (D) return D;
            std::string Upper;
            for (char C : Base) Upper += (char)toupper(C);
            D = Archive->ReadFile(Upper);
            if (D) return D;
            std::string Lower;
            for (char C : Base) Lower += (char)tolower(C);
            D = Archive->ReadFile(Lower);
            if (D) return D;
            return {};
        };

        auto ReadWithFallback = [&](const std::string& Path) -> std::optional<std::vector<uint8_t>>
        {
            auto Data = TryPaths(Path, mArchive);
            if (!Data && mArchive2)
                Data = TryPaths(Path, mArchive2);
            if (!Data)
                Data = TryPaths("arr/" + Path, mArchive);
            if (!Data && mArchive2)
                Data = TryPaths("arr/" + Path, mArchive2);
            if (!Data)
                Data = TryPaths("data/" + Path, mArchive);
            if (!Data && mArchive2)
                Data = TryPaths("data/" + Path, mArchive2);
            return Data;
        };

        auto LoadTable = [&](const std::string& Name, auto LoadFn) {
            auto Data = ReadWithFallback(Name);
            if (Data) LoadFn(*Data);
            else Core::Utils::Logger::Warning("ResourceCache", "Cannot load: " + Name);
        };

        LoadTable("units.dat", [&](auto& D) { mDataLoader.LoadUnitDataRaw(D); });
        LoadTable("weapons.dat", [&](auto& D) { mDataLoader.LoadWeaponDataRaw(D); });
        LoadTable("flingy.dat", [&](auto& D) { mDataLoader.LoadFlingyDataRaw(D); });
        LoadTable("sprites.dat", [&](auto& D) { mDataLoader.LoadSpriteDataRaw(D); });
        LoadTable("images.dat", [&](auto& D) { mDataLoader.LoadImageDataRaw(D); });
        LoadTable("upgrades.dat", [&](auto& D) { mDataLoader.LoadUpgradeDataRaw(D); });
        LoadTable("techdata.dat", [&](auto& D) { mDataLoader.LoadTechDataRaw(D); });
        LoadTable("images.tbl", [&](auto& D) { mDataLoader.LoadImageTblDataRaw(D); });
        mDataLoader.RebuildImageMappingFromTbl();

        mPalette = mDataLoader.LoadUnitPalette(*mArchive);
        if ((mPalette.Colors[1].R == 0 && mPalette.Colors[1].G == 0 && mPalette.Colors[1].B == 0)
            && mArchive2)
        {
            mPalette = mDataLoader.LoadUnitPalette(*mArchive2);
        }
    }

    Core::Utils::Logger::Info("ResourceCache",
        "Palette Colors[0]=( " + std::to_string(mPalette.Colors[0].R) + "," +
        std::to_string(mPalette.Colors[0].G) + "," +
        std::to_string(mPalette.Colors[0].B) + ")");

    LoadUIGraphics();

    mIsInitialized = true;
    Core::Utils::Logger::Info("ResourceCache", "Initialization complete");
    return true;
}

const std::vector<Texture2D>* ResourceCache::GetSpriteTextures(uint32_t ImageID)
{
    auto It = mTextureCache.find(ImageID);
    if (It != mTextureCache.end())
        return &It->second;

    if (!mArchive)
        return nullptr;

    std::string GrpPath = mDataLoader.ResolveGrpPath(ImageID);
    if (GrpPath.empty())
        return nullptr;

    // Try primary archive with case variants, then secondary
    auto TryGrp = [&](const std::string& Path, auto& Archive) -> std::optional<std::vector<uint8_t>>
    {
        auto D = Archive->ReadFile(Path);
        if (D) return D;
        // Try uppercase
        std::string Upper;
        for (char C : Path) Upper += (char)toupper(C);
        D = Archive->ReadFile(Upper);
        if (D) return D;
        return {};
    };

    auto GrpData = TryGrp(GrpPath, mArchive);
    if (!GrpData && mArchive2)
        GrpData = TryGrp(GrpPath, mArchive2);
    if (!GrpData)
        GrpData = TryGrp("arr/" + GrpPath, mArchive);
    if (!GrpData && mArchive2)
        GrpData = TryGrp("arr/" + GrpPath, mArchive2);
    if (!GrpData)
        GrpData = TryGrp("data/" + GrpPath, mArchive);
    if (!GrpData && mArchive2)
        GrpData = TryGrp("data/" + GrpPath, mArchive2);

    if (!GrpData)
    {
        Core::Utils::Logger::Warning("ResourceCache",
            "GRP not found: " + GrpPath + " (ImageID=" + std::to_string(ImageID) + ")");
        return nullptr;
    }

    auto TexturesOpt = DecodeGrpToRaylibTextures(*GrpData, mPalette);
    if (!TexturesOpt)
    {
        Core::Utils::Logger::Warning("ResourceCache",
            "Failed to decode GRP: " + GrpPath + " (ImageID=" + std::to_string(ImageID) + ")");
        return nullptr;
    }

    auto& Cached = mTextureCache[ImageID];
    Cached = std::move(*TexturesOpt);

    if (ImageID >= 100 && ImageID <= 103)
    {
        Core::Utils::Logger::Info("ResourceCache",
            "Resource GRP: ImageID=" + std::to_string(ImageID) +
            " -> " + GrpPath + " (" + std::to_string(Cached.size()) + " frames)");
    }

    return &Cached;
}

bool ResourceCache::SetTilesetPalette(const std::string& TilesetName)
{
    if (!mArchive && !mArchive2)
        return false;

    auto ReadFile = [&](const std::string& Path) -> std::optional<std::vector<uint8_t>>
    {
        auto D = mArchive3 ? mArchive3->ReadFile(Path) : std::nullopt;
        if (!D && mArchive) D = mArchive->ReadFile(Path);
        if (!D && mArchive2) D = mArchive2->ReadFile(Path);
        return D;
    };

    // Try Tileset/<name>.wpe
    std::string WpePath = "Tileset/" + TilesetName + ".wpe";
    auto WpeData = ReadFile(WpePath);
    if (!WpeData)
    {
        WpePath = "TILESET/" + TilesetName + ".WPE";
        WpeData = ReadFile(WpePath);
    }

    if (!WpeData)
    {
        Core::Utils::Logger::Warning("ResourceCache",
            "WPE not found for tileset: " + TilesetName);
        return false;
    }

    // Decode WPE palette
    auto PalOpt = PalDecoder::DecodeWpe(*WpeData);
    if (!PalOpt)
    {
        Core::Utils::Logger::Warning("ResourceCache",
            "Failed to decode WPE: " + WpePath);
        return false;
    }

    mPalette = *PalOpt;
    Core::Utils::Logger::Info("ResourceCache",
        "Set tileset palette: " + TilesetName +
        " Colors[0]=" + std::to_string(mPalette.Colors[0].R) + "," +
        std::to_string(mPalette.Colors[0].G) + "," +
        std::to_string(mPalette.Colors[0].B));

    // Clear cached tileset textures so they rebuild with new palette
    mTilesetCache.clear();
    return true;
}

ResourceCache::TilesetData* ResourceCache::GetTilesetData(const std::string& TilesetName)
{
    auto It = mTilesetData.find(TilesetName);
    if (It != mTilesetData.end())
        return &It->second;
    return nullptr;
}

ResourceCache::TilesetData* ResourceCache::LoadTilesetData(const std::string& TilesetName)
{
    auto It = mTilesetData.find(TilesetName);
    if (It != mTilesetData.end())
        return &It->second;

    if (!mArchive && !mArchive2 && !mArchive3)
        return nullptr;

    auto ReadFile = [&](const std::string& Path) -> std::optional<std::vector<uint8_t>>
    {
        auto D = mArchive3 ? mArchive3->ReadFile(Path) : std::nullopt;
        if (!D && mArchive) D = mArchive->ReadFile(Path);
        if (!D && mArchive2) D = mArchive2->ReadFile(Path);
        return D;
    };

    // Load VX4 (megatile definitions: 16 uint16 per megatile)
    auto Vx4Data = ReadFile("Tileset/" + TilesetName + ".vx4");
    if (!Vx4Data) Vx4Data = ReadFile("TILESET/" + TilesetName + ".VX4");
    if (!Vx4Data) return nullptr;

    // Load VR4 (8x8 mini-tile pixel data: 64 bytes per mini-tile)
    auto Vr4Data = ReadFile("Tileset/" + TilesetName + ".vr4");
    if (!Vr4Data) Vr4Data = ReadFile("TILESET/" + TilesetName + ".VR4");
    if (!Vr4Data) return nullptr;

    // Load CV5 (maps tile group+subtile → megatile index)
    // CV5: 52 bytes per entry, containing 16 uint16 mega_tile_index values
    auto Cv5Data = ReadFile("Tileset/" + TilesetName + ".cv5");
    if (!Cv5Data) Cv5Data = ReadFile("TILESET/" + TilesetName + ".CV5");

    TilesetData& Data = mTilesetData[TilesetName];
    auto Vx4Result = Vx4Decoder::Decode(*Vx4Data);
    Data.Vx4.clear();
    Data.Vx4.reserve(Vx4Result.MegaTileCount * 16);
    for (auto& M : Vx4Result.MegaTiles)
    {
        for (int J = 0; J < 16; J++)
            Data.Vx4.push_back(M.MiniTileIDs[J]);
    }
    Data.Vr4 = std::move(*Vr4Data);

    if (Cv5Data)
    {
        auto Cv5Entries = Cv5Decoder::Decode(*Cv5Data);
        Data.Cv5.clear();
        Data.Cv5.reserve(Cv5Entries.size() * 16);
        for (auto& E : Cv5Entries)
        {
            for (int J = 0; J < 16; J++)
                Data.Cv5.push_back(E.MegaTileIndices[J]);
        }
    }

    // Load VF4 from both archives with all path variants
    auto Vf4File = ReadFile("Tileset/" + TilesetName + ".vf4");
    if (!Vf4File) Vf4File = ReadFile("TILESET/" + TilesetName + ".VF4");
    if (!Vf4File)
    {
        std::string BsPath = "tileset\\" + TilesetName + ".vf4";
        Vf4File = ReadFile(BsPath);
    }
    if (Vf4File)
    {
        auto Vf4Result = Vf4Decoder::Decode(*Vf4File);
        Data.Vf4.clear();
        Data.Vf4.reserve(Vf4Result.Entries.size() * 16);
        for (auto& E : Vf4Result.Entries)
            for (int J = 0; J < 16; J++)
                Data.Vf4.push_back(E.Flags[J]);
    }
    else
    {
        size_t MiniTileCount = Data.Vr4.size() / 64;
        std::vector<bool> MiniWalkable(MiniTileCount, false);
        for (size_t M = 0; M < MiniTileCount; M++)
        {
            uint8_t Counts[256] = {0};
            int NonZero = 0;
            for (int P = 0; P < 64; P++)
            {
                uint8_t V = Data.Vr4[M * 64 + P];
                if (V != 0 && Counts[V] == 0) NonZero++;
                Counts[V]++;
            }
            // Walkable if at least 3 distinct non-zero colors in the 8x8 tile
            MiniWalkable[M] = (NonZero >= 3);
        }

        // Build VF4 per megatile: a megatile has 16 minitiles (4x4 grid from VX4)
        size_t MegaTileCount = Data.Vx4.size() / 16;
        Data.Vf4.clear();
        Data.Vf4.reserve(MegaTileCount * 16);
        for (size_t M = 0; M < MegaTileCount; M++)
        {
            for (int J = 0; J < 16; J++)
            {
                uint16_t MiniIdx = Data.Vx4[M * 16 + J];
                uint16_t Vr4Entry = MiniIdx / 2;
                bool Walkable = (Vr4Entry < MiniWalkable.size()) ? MiniWalkable[Vr4Entry] : true;
                Data.Vf4.push_back(Walkable ? 1 : 0);
            }
        }
        Core::Utils::Logger::Info("ResourceCache",
            "Generated VF4 from VR4: " + std::to_string(MegaTileCount) +
            " megatiles (" + std::to_string(MiniTileCount) + " minitiles)");
    }

    Data.Loaded = true;

    Core::Utils::Logger::Info("ResourceCache",
        "Loaded tileset " + TilesetName + ": VX4=" +
        std::to_string(Data.Vx4.size() / 16) + " entries, CV5=" +
        std::to_string(Data.Cv5.size() / 16) + " groups, VR4=" +
        std::to_string(Data.Vr4.size() / 64) + " tiles, VF4=" +
        std::to_string(Data.Vf4.size() / 16) + " entries");

    return &Data;
}

const std::vector<Texture2D>* ResourceCache::GetTilesetTextures(const std::string& TilesetName)
{
    auto CacheIt = mTilesetCache.find(TilesetName);
    if (CacheIt != mTilesetCache.end())
        return &CacheIt->second;

    auto* Tileset = LoadTilesetData(TilesetName);
    if (!Tileset || !Tileset->Loaded)
        return nullptr;

    size_t MegatileCount = Tileset->Vx4.size() / 16;
    auto& Textures = mTilesetCache[TilesetName];

    Core::Utils::Logger::Info("ResourceCache",
        "Building tileset textures for " + TilesetName +
        " (" + std::to_string(MegatileCount) + " megatiles)...");

    constexpr int MegaW = 32, MegaH = 32;
    constexpr int MiniW = 8, MiniH = 8;
    std::vector<uint8_t> Pixels(MegaW * MegaH * 4, 0);

    Core::Utils::Logger::Info("ResourceCache",
        "Building " + std::to_string(MegatileCount) + " megatile textures...");

    for (size_t M = 0; M < MegatileCount; M++)
    {
        memset(Pixels.data(), 0, Pixels.size());

        for (int Iy = 0; Iy < 4; Iy++)
        {
            for (int Ix = 0; Ix < 4; Ix++)
            {
                size_t Vx4Off = M * 16 + Iy * 4 + Ix;
                if (Vx4Off >= Tileset->Vx4.size()) continue;
                uint16_t ImgIdx = Tileset->Vx4[Vx4Off];
                size_t Vr4Entry = ImgIdx / 2;
                bool Inverted = (ImgIdx & 1) != 0;

                size_t Vr4Off = Vr4Entry * 64;
                if (Vr4Off + 64 > Tileset->Vr4.size()) continue;

                for (int Py = 0; Py < MiniH; Py++)
                {
                    for (int Px = 0; Px < MiniW; Px++)
                    {
                        int SrcPx = Inverted ? (MiniW - 1 - Px) : Px;
                        uint8_t PalIdx = (uint8_t)Tileset->Vr4[Vr4Off + Py * MiniW + SrcPx];

                        int DstX = Ix * MiniW + Px;
                        int DstY = Iy * MiniH + Py;
                        int DstOff = (DstY * MegaW + DstX) * 4;

                        const auto& C = mPalette.Colors[PalIdx];
                        Pixels[DstOff + 0] = C.R;
                        Pixels[DstOff + 1] = C.G;
                        Pixels[DstOff + 2] = C.B;
                        Pixels[DstOff + 3] = C.A;
                    }
                }
            }
        }

        Image Img = {};
        Img.width = MegaW;
        Img.height = MegaH;
        Img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        Img.mipmaps = 1;
        Img.data = malloc(Pixels.size());
        memcpy(Img.data, Pixels.data(), Pixels.size());
        Texture2D Tex = LoadTextureFromImage(Img);
        free(Img.data);
        Textures.push_back(Tex);
    }

    Core::Utils::Logger::Info("ResourceCache",
        "Built tileset " + TilesetName + ": " +
        std::to_string(Textures.size()) + " megatiles");

    return &Textures;
}

bool ResourceCache::LoadUIGraphics()
{
    auto ReadFn = [&](const std::string& P) -> std::optional<std::vector<uint8_t>> {
        auto D = mArchive3 ? mArchive3->ReadFile(P) : std::nullopt;
        if (!D && mArchive) D = mArchive->ReadFile(P);
        if (!D && mArchive2) D = mArchive2->ReadFile(P);
        return D;
    };

    // Load icons.grp (12 frames, 64x64 each)
    auto IconsData = ReadFn("game\\icons.grp");
    if (IconsData)
    {
        auto TexOpt = DecodeGrpToRaylibTextures(*IconsData, mPalette);
        if (TexOpt) mUIIcons = std::move(*TexOpt);
    }

    auto BlinkData = ReadFn("game\\blink.grp");
    if (BlinkData)
    {
        auto TexOpt = DecodeGrpToRaylibTextures(*BlinkData, mPalette);
        if (TexOpt && !TexOpt->empty()) mUIBlink = (*TexOpt)[0];
    }

    {
        auto D = ReadFn("game\\nconsole.pcx");
        if (D)
        {
            auto PCX = PcxDecoder::Decode(*D);
            if (PCX.Texture.Valid())
            {
                Image RlImg = ToRaylibImage(PCX.Texture);
                mUIConsole = LoadTextureFromImage(RlImg);
                mUIRegions = PCX.Regions;
                UnloadImage(RlImg);
            }
        }
    }

    Core::Utils::Logger::Info("ResourceCache",
        "UI graphics: " + std::to_string(mUIIcons.size()) + " icons" +
        (mUIBlink.id > 0 ? ", blink" : "") +
        (mUIConsole.id > 0 ? ", console" : "") +
        (mUIConsoleOver.id > 0 ? ", conover" : ""));
    return true;
}

std::optional<std::vector<uint8_t>> ResourceCache::ReadFile(const std::string& Path)
{
    auto Data = mArchive3 ? mArchive3->ReadFile(Path) : std::nullopt;
    if (!Data && mArchive) Data = mArchive->ReadFile(Path);
    if (!Data && mArchive2) Data = mArchive2->ReadFile(Path);
    return Data;
}

bool ResourceCache::OpenAssetsDirectory(const std::string& AssetsPath)
{
    auto Reader = std::make_unique<FileSystemArchive>();
    if (Reader->Open(AssetsPath))
    {
        mArchive = std::move(Reader);
        Core::Utils::Logger::Info("ResourceCache", "Opened assets directory: " + AssetsPath);
        return true;
    }
    return false;
}

} // namespace Resource
