#include "Resource/GameDetector.h"
#include "Core/Utils/Logger.h"

#ifdef _WIN32
#include <windows.h>
#include <winreg.h>
#endif

#include <cstdio>
#include <sys/stat.h>

namespace Resource
{

static bool FileExists(const std::string& Path)
{
    struct stat Buf;
    return stat(Path.c_str(), &Buf) == 0;
}

bool GameDetector::CheckMpqExists(const std::string& Dir, const std::string& MpqName)
{
    std::string Path = Dir + "/" + MpqName;
    if (FileExists(Path))
        return true;
    Path = Dir + "\\" + MpqName;
    return FileExists(Path);
}

bool GameDetector::CheckCascExists(const std::string& Dir)
{
    // CASC storage has a data/ directory with .idx files
    // StarCraft: Remastered uses "Data/data/" (capital D), classic CASC uses "data/"
    std::vector<std::string> Candidates = {
        Dir + "/Data/data/data.000",
        Dir + "\\Data\\data\\data.000",
        Dir + "/data/data.000",
        Dir + "\\data\\data.000",
    };
    for (auto& C : Candidates)
    {
        if (FileExists(C))
            return true;
    }
    return false;
}

std::vector<std::string> GameDetector::GetCommonPaths()
{
    std::vector<std::string> Paths;

#ifdef _WIN32
    // Check registry
    HKEY Keys[] = {
        HKEY_LOCAL_MACHINE,
        HKEY_CURRENT_USER
    };

    const char* SubKeys[] = {
        "SOFTWARE\\Blizzard Entertainment\\StarCraft",
        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\StarCraft",
        "SOFTWARE\\Wow6432Node\\Blizzard Entertainment\\StarCraft"
    };

    for (auto HKey : Keys)
    {
        for (const char* SubKey : SubKeys)
        {
            HKEY RegKey;
            if (RegOpenKeyExA(HKey, SubKey, 0, KEY_READ | KEY_WOW64_64KEY, &RegKey) == ERROR_SUCCESS)
            {
                char Value[512] = {};
                DWORD ValueSize = sizeof(Value);
                DWORD Type = 0;
                if (RegQueryValueExA(RegKey, "InstallPath", nullptr, &Type,
                                     (LPBYTE)Value, &ValueSize) == ERROR_SUCCESS)
                {
                    Paths.push_back(Value);
                }
                RegCloseKey(RegKey);
            }
        }
    }

    // Common paths
    Paths.push_back("C:\\Program Files\\StarCraft");
    Paths.push_back("C:\\Program Files (x86)\\StarCraft");
    Paths.push_back("C:\\Program Files\\StarCraft Remastered");
    Paths.push_back("C:\\Program Files (x86)\\StarCraft Remastered");
#endif

    // Also check current directory and common relative paths
    // VS2022 working dir may be project root or build output dir,
    // so try multiple relative depths.
    Paths.push_back(".");
    Paths.push_back("./Resources");
    Paths.push_back("../Resources");
    Paths.push_back("../../Resources");
    Paths.push_back("../../../Resources");
    Paths.push_back("./StarCraft");
    Paths.push_back("./StarCraft Remastered");

#ifdef _WIN32
    // Try location relative to the executable itself
    // Typical layout: C:\Projects\StarCraft\out\build\x64-Debug\StarcraftRemake.exe
    // Resources is at C:\Projects\StarCraft\Resources
    char ExePath[MAX_PATH];
    GetModuleFileNameA(nullptr, ExePath, MAX_PATH);
    std::string ExeDir = ExePath;
    auto Pos = ExeDir.find_last_of("\\/");
    if (Pos != std::string::npos)
    {
        ExeDir = ExeDir.substr(0, Pos);
        Paths.push_back(ExeDir + "/Resources");
        Paths.push_back(ExeDir + "/../Resources");
        Paths.push_back(ExeDir + "/../../Resources");
        Paths.push_back(ExeDir + "/../../../Resources");
    }
#endif

    return Paths;
}

GameInstallInfo GameDetector::CheckPath(const std::string& Path)
{
    GameInstallInfo Info;
    Info.InstallPath = Path;

    // Check for classic MPQ files
    if (CheckMpqExists(Path, "StarDat.mpq") ||
        CheckMpqExists(Path, "BrooDat.mpq") ||
        CheckMpqExists(Path, "patch_rt.mpq") ||
        CheckMpqExists(Path, "install.exe"))
    {
        Info.Type = ArchiveType::Mpq;
        Info.StarDatPath = Path + "/StarDat.mpq";
        Info.BrooDatPath = Path + "/BrooDat.mpq";
        Info.PatchRtPath = Path + "/patch_rt.mpq";

        Core::Utils::Logger::Info("GameDetector",
            "Found classic StarCraft at: " + Path);
        return Info;
    }

    // Check for Remastered CASC storage
    if (CheckCascExists(Path))
    {
        Info.Type = ArchiveType::Casc;

        // Determine the CASC root (directory containing data/, indices/, config/)
        if (FileExists(Path + "/Data/data/data.000") || FileExists(Path + "\\Data\\data\\data.000"))
            Info.CascDataPath = Path + "/Data";
        else
            Info.CascDataPath = Path + "/data";

        Core::Utils::Logger::Info("GameDetector",
            "Found StarCraft Remastered at: " + Info.CascDataPath);
        return Info;
    }

    return Info;  // Invalid
}

GameInstallInfo GameDetector::Detect()
{
    Core::Utils::Logger::Info("GameDetector",
        "Detecting StarCraft installation...");

    auto Paths = GetCommonPaths();
    for (const auto& Path : Paths)
    {
        auto Info = CheckPath(Path);
        if (Info.IsValid())
            return Info;
    }

    Core::Utils::Logger::Warning("GameDetector",
        "StarCraft installation not found automatically");
    return {};
}

} // namespace Resource
