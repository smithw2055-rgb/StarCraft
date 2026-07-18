#include "Game/IScriptAngelScriptExporter.h"

#include <fstream>
#include <iostream>
#include <string>

namespace
{
    void PrintUsage()
    {
        std::cerr << "Usage: IScriptToAngelScript [iscript.bin] [images.dat] [output.as]\n"
                  << "Defaults: assets/scripts/iscript.bin assets/arr/images.dat "
                     "assets/scripts/iscript.as\n";
    }
}

int main(int Argc, char** Argv)
{
    if (Argc > 1 && std::string(Argv[1]) == "--help")
    {
        PrintUsage();
        return 0;
    }
    if (Argc > 4)
    {
        PrintUsage();
        return 2;
    }

    const std::string IScriptPath = Argc > 1 ? Argv[1] : "assets/scripts/iscript.bin";
    const std::string ImagesDatPath = Argc > 2 ? Argv[2] : "assets/arr/images.dat";
    const std::string OutputPath = Argc > 3 ? Argv[3] : "assets/scripts/iscript.as";

    Game::IScriptDatabase Database;
    if (!Database.Load(IScriptPath, ImagesDatPath))
    {
        std::cerr << "Failed to load IScript resources:\n"
                  << "  iscript: " << IScriptPath << "\n"
                  << "  images:  " << ImagesDatPath << "\n";
        return 1;
    }

    std::ofstream Output(OutputPath, std::ios::binary | std::ios::trunc);
    if (!Output)
    {
        std::cerr << "Failed to open output: " << OutputPath << "\n";
        return 1;
    }

    Game::IScriptAngelScriptExportStats Stats;
    std::string Error;
    if (!Game::ExportIScriptToAngelScript(Database.GetProgram(), Output, &Stats, &Error))
    {
        std::cerr << "Failed to export AngelScript: " << Error << "\n";
        return 1;
    }
    Output.close();

    std::cout << "Generated " << OutputPath << "\n"
              << "  scripts:       " << Stats.ScriptCount << "\n"
              << "  animation entries: " << Stats.AnimationEntryCount << "\n"
              << "  instructions:  " << Stats.InstructionCount << "\n"
              << "  program words:  " << Stats.ProgramWordCount << "\n";
    return 0;
}
