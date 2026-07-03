#include "Client/MapLoader.h"

#include "Resource/MpqArchive.h"
#include "Resource/CascArchive.h"
#include "Resource/GameDetector.h"

#include "Core/Utils/Logger.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Game::Client
{

std::vector<uint8_t> MapLoader::sLastChkData;

namespace
{
    // Convert CHK start location (x,y in pixels) to tile coords
    void ChkStartToTile(const std::vector<Game::Map::StartLocation>& In,
                        std::vector<StartLocation>& Out)
    {
        Out.clear();
        Out.reserve(In.size());
        for (const auto& S : In)
        {
            StartLocation O;
            O.TileX = S.X / 32;
            O.TileY = S.Y / 32;
            O.PlayerId = S.Owner;
            Out.push_back(O);
        }
    }

    std::string GetExecutableDir()
    {
#ifdef _WIN32
        char Buffer[MAX_PATH] = {};
        DWORD Length = GetModuleFileNameA(nullptr, Buffer, MAX_PATH);
        if (Length == 0 || Length >= MAX_PATH) return {};
        return std::filesystem::path(Buffer).parent_path().string();
#else
        return {};
#endif
    }

#ifdef _WIN32
    std::optional<std::vector<uint8_t>> TryReadChkWithStormLib(const std::string& ArchivePath,
                                                               std::string& OutChkPath)
    {
        HMODULE StormLib = LoadLibraryA("StormLib.dll");
        if (!StormLib)
        {
            Core::Utils::Logger::Warning("MapLoader", "StormLib.dll not found for map archive fallback");
            return std::nullopt;
        }

        using SFileOpenArchiveFn = BOOL (WINAPI*)(LPCSTR, DWORD, DWORD, HANDLE*);
        using SFileOpenFileExFn = BOOL (WINAPI*)(HANDLE, LPCSTR, DWORD, HANDLE*);
        using SFileGetFileSizeFn = DWORD (WINAPI*)(HANDLE, LPDWORD);
        using SFileReadFileFn = BOOL (WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
        using SFileCloseFileFn = BOOL (WINAPI*)(HANDLE);
        using SFileCloseArchiveFn = BOOL (WINAPI*)(HANDLE);

        auto SFileOpenArchivePtr = reinterpret_cast<SFileOpenArchiveFn>(
            GetProcAddress(StormLib, "SFileOpenArchive"));
        auto SFileOpenFileExPtr = reinterpret_cast<SFileOpenFileExFn>(
            GetProcAddress(StormLib, "SFileOpenFileEx"));
        auto SFileGetFileSizePtr = reinterpret_cast<SFileGetFileSizeFn>(
            GetProcAddress(StormLib, "SFileGetFileSize"));
        auto SFileReadFilePtr = reinterpret_cast<SFileReadFileFn>(
            GetProcAddress(StormLib, "SFileReadFile"));
        auto SFileCloseFilePtr = reinterpret_cast<SFileCloseFileFn>(
            GetProcAddress(StormLib, "SFileCloseFile"));
        auto SFileCloseArchivePtr = reinterpret_cast<SFileCloseArchiveFn>(
            GetProcAddress(StormLib, "SFileCloseArchive"));

        if (!SFileOpenArchivePtr || !SFileOpenFileExPtr || !SFileGetFileSizePtr ||
            !SFileReadFilePtr || !SFileCloseFilePtr || !SFileCloseArchivePtr)
        {
            Core::Utils::Logger::Warning("MapLoader", "StormLib.dll missing required exports");
            FreeLibrary(StormLib);
            return std::nullopt;
        }

        HANDLE Archive = nullptr;
        if (!SFileOpenArchivePtr(ArchivePath.c_str(), 0, 0, &Archive))
        {
            std::string AltPath = ArchivePath;
            for (auto& C : AltPath) if (C == '/') C = '\\';
            if (AltPath == ArchivePath || !SFileOpenArchivePtr(AltPath.c_str(), 0, 0, &Archive))
            {
                Core::Utils::Logger::Warning("MapLoader", "StormLib fallback cannot open archive: " + ArchivePath);
                FreeLibrary(StormLib);
                return std::nullopt;
            }
        }

        std::optional<std::vector<uint8_t>> Result;
        for (const char* ChkPath : { "staredit\\scenario.chk", "scenario.chk" })
        {
            HANDLE File = nullptr;
            if (!SFileOpenFileExPtr(Archive, ChkPath, 0, &File))
                continue;

            DWORD HighSize = 0;
            DWORD Size = SFileGetFileSizePtr(File, &HighSize);
            if (HighSize == 0 && Size != INVALID_FILE_SIZE)
            {
                std::vector<uint8_t> Buffer(Size);
                DWORD BytesRead = 0;
                if (SFileReadFilePtr(File, Buffer.data(), Size, &BytesRead, nullptr) && BytesRead == Size)
                {
                    OutChkPath = ChkPath;
                    Result = std::move(Buffer);
                }
            }

            SFileCloseFilePtr(File);
            if (Result)
                break;
        }

        SFileCloseArchivePtr(Archive);
        FreeLibrary(StormLib);
        if (Result)
        {
            Core::Utils::Logger::Info("MapLoader",
                "Loaded scenario chunk via StormLib fallback: " + OutChkPath);
        }
        return Result;
    }
#endif
}

std::string MapLoader::FindDefaultMap(const std::string& InstallPath)
{
    static const char* kCandidates[] = {
        "./assets/locales/zhCN/Assets/campaign/Terran/Terran01/staredit/scenario.chk",
        "./assets/locales/enUS/Assets/campaign/Zerg/tutorial/staredit/scenario.chk",
        "./assets/locales/zhCN/Assets/campaign/Zerg/tutorial/staredit/scenario.chk",
        "(2)Astral Balance.scm",
        "(2)Astral Balance.scx",
        "(4)Andromeda.scx",
        "(2)Ancient City.scm",
        nullptr
    };
    std::vector<std::string> Dirs = {
        "",
        "./assets/maps/BroodWar/",
        "./assets/maps/",
    };

    const std::string ExeDir = GetExecutableDir();
    if (!ExeDir.empty())
    {
        std::filesystem::path Base(ExeDir);
        Dirs.push_back((Base / "assets/maps/BroodWar").string() + "/");
        Dirs.push_back((Base / "assets/maps").string() + "/");
        Dirs.push_back((Base / "../assets/maps/BroodWar").lexically_normal().string() + "/");
        Dirs.push_back((Base / "../assets/maps").lexically_normal().string() + "/");
    }
    Dirs.push_back(InstallPath + "/maps/BroodWar/");
    Dirs.push_back(InstallPath + "/maps/");
    Dirs.push_back(InstallPath + "/Maps/BroodWar/");
    Dirs.push_back(InstallPath + "/Maps/");
    Dirs.push_back("./Resources/Maps/BroodWar/");
    Dirs.push_back("./Resources/Maps/");
    Dirs.push_back("./maps/BroodWar/");
    Dirs.push_back("./maps/");
    for (auto& Dir : Dirs)
    {
        for (int I = 0; kCandidates[I]; ++I)
        {
            std::string P = Dir + kCandidates[I];
            struct stat Buf;
            if (stat(P.c_str(), &Buf) == 0)
            {
                Core::Utils::Logger::Info("MapLoader", "Selected default map: " + P);
                return P;
            }
            std::string Alt = Dir;
            for (auto& C : Alt) if (C == '/') C = '\\';
            P = Alt + kCandidates[I];
            if (stat(P.c_str(), &Buf) == 0)
            {
                Core::Utils::Logger::Info("MapLoader", "Selected default map: " + P);
                return P;
            }
        }
    }
    return {};
}

MapLoader::Result MapLoader::Load(const std::string& ScmPath)
{
    Result Out;
    sLastChkData.clear();
    if (ScmPath.empty())
    {
        Out.ErrorMessage = "empty map path";
        return Out;
    }
    struct stat Buf;
    if (stat(ScmPath.c_str(), &Buf) != 0)
    {
        Out.ErrorMessage = "file not found: " + ScmPath;
        return Out;
    }

    std::string LowerPath = ScmPath;
    std::transform(LowerPath.begin(), LowerPath.end(), LowerPath.begin(),
        [](unsigned char C) { return (char)std::tolower(C); });

    std::vector<uint8_t> DirectChkData;
    std::optional<std::vector<uint8_t>> ChkData;
    if (LowerPath.size() >= 4 && LowerPath.substr(LowerPath.size() - 4) == ".chk")
    {
        std::ifstream File(ScmPath, std::ios::binary | std::ios::ate);
        if (!File.is_open())
        {
            Out.ErrorMessage = "cannot open chk file: " + ScmPath;
            return Out;
        }

        std::streamsize Size = File.tellg();
        File.seekg(0, std::ios::beg);
        DirectChkData.resize((size_t)Size);
        if (!File.read(reinterpret_cast<char*>(DirectChkData.data()), Size))
        {
            Out.ErrorMessage = "cannot read chk file: " + ScmPath;
            return Out;
        }

        ChkData = DirectChkData;
        Out.ChkPath = ScmPath;
        Core::Utils::Logger::Info("MapLoader", "Opened direct CHK: " + ScmPath);
    }
    else
    {
        Core::Utils::Logger::Info("MapLoader", "Opening map archive: " + ScmPath);

        auto Arc = Resource::CreateBestArchiveForPath(ScmPath);
        if (!Arc)
        {
            std::string FallbackChkPath;
#ifdef _WIN32
            ChkData = TryReadChkWithStormLib(ScmPath, FallbackChkPath);
#endif
            if (!ChkData)
            {
                Out.ErrorMessage = "cannot open archive: " + ScmPath;
                Core::Utils::Logger::Warning("MapLoader", Out.ErrorMessage);
                return Out;
            }

            Core::Utils::Logger::Info("MapLoader", "Using scenario chunk: " + FallbackChkPath);
            Out.ChkPath = FallbackChkPath;
        }
        else
        {
            Core::Utils::Logger::Info("MapLoader",
                "Map archive reader type: " + Arc->GetArchiveTypeName());

            auto Files = Arc->ListFiles();
            std::string ChkPath;
            for (auto& F : Files)
            {
                std::string L = F.FilePath;
                for (auto& C : L) C = (char)std::tolower((unsigned char)C);
                if (L.find("scenario.chk") != std::string::npos)
                {
                    ChkPath = F.FilePath;
                    break;
                }
            }
            if (ChkPath.empty()) ChkPath = "staredit\\scenario.chk";

            ChkData = Arc->ReadFile(ChkPath);
            if (!ChkData)
            {
                ChkPath = "scenario.chk";
                ChkData = Arc->ReadFile(ChkPath);
            }
            Arc->Close();

            if (!ChkData)
            {
                Out.ErrorMessage = "scenario.chk not found in archive";
                Core::Utils::Logger::Warning("MapLoader", Out.ErrorMessage + ": " + ScmPath);
                return Out;
            }

            Core::Utils::Logger::Info("MapLoader", "Using scenario chunk: " + ChkPath);
            Out.ChkPath = ChkPath;
        }
    }

    Game::Map::ChkParser Parser;
    // Store raw CHK data for trigger/replay parsing
    sLastChkData = *ChkData;
    Out.RawChkData = *ChkData;
    if (!Parser.Parse(ChkData->data(), ChkData->size()))
    {
        Out.ErrorMessage = "CHK parse failed";
        return Out;
    }

    Out.Map.Width = Parser.GetMapWidth();
    Out.Map.Height = Parser.GetMapHeight();
    Out.Map.MegaTileIds = Parser.GetMegaTiles();
    Out.Map.Walkable.assign(static_cast<size_t>(Out.Map.Width) * Out.Map.Height, 1);

    static const char* TilesetNames[] = {
        "badlands", "platform", "install", "ashworld",
        "jungle", "desert", "ice", "twilight"
    };
    int Ts = Parser.GetTileset();
    if (Ts >= 0 && Ts < 8) Out.Map.TilesetName = TilesetNames[Ts];

    // Translate ChkParser's start locations to GameMap start locations.
    ChkStartToTile({}, Out.Map.StartLocations); // noop - we'll set from ChkParser below

    // Parse start locations (unit ID 214) and the placed-units array
    Out.ChkUnits.assign(Parser.GetUnits(), Parser.GetUnits() + Parser.GetUnitCount());
    Out.ChkStartLocations.clear();
    for (const auto& U : Out.ChkUnits)
    {
        if (U.UnitID == 214)
        {
            StartLocation S;
            S.TileX = U.X / 32;
            S.TileY = U.Y / 32;
            S.PlayerId = U.Owner;
            Out.ChkStartLocations.push_back(S);
            Out.Map.StartLocations.push_back(S);
        }
    }

    Out.Loaded = true;
    Core::Utils::Logger::Info("MapLoader",
        std::string("Loaded ") + ScmPath + ": " +
        std::to_string(Out.Map.Width) + "x" + std::to_string(Out.Map.Height) + " tiles, " +
        std::to_string(Out.ChkUnits.size()) + " units, tileset=" + Out.Map.TilesetName);
    return Out;
}

} // namespace Game::Client
