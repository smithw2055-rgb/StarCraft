#include "Game/IScriptAngelScriptExporter.h"

#include <iostream>
#include <sstream>
#include <string>

namespace
{
    bool Require(bool Condition, const char* Message)
    {
        if (Condition)
            return true;
        std::cerr << "FAIL: " << Message << "\n";
        return false;
    }
}

int main()
{
    Game::IScriptDatabase Database;
    if (!Require(Database.Load("assets/scripts/iscript.bin", "assets/arr/images.dat"),
                 "iscript.bin and images.dat load"))
        return 1;

    const Game::IScriptProgram& Program = Database.GetProgram();
    std::ostringstream Source;
    Game::IScriptAngelScriptExportStats Stats;
    std::string Error;
    if (!Require(Game::ExportIScriptToAngelScript(Program, Source, &Stats, &Error),
                 Error.empty() ? "AngelScript export" : Error.c_str()))
        return 1;

    const std::string Text = Source.str();
    size_t ExpectedAnimationEntries = 0;
    for (const auto& Pair : Program.Scripts)
        ExpectedAnimationEntries += Pair.second.AnimationPC.size();

    bool Ok = true;
    Ok &= Require(!Program.Scripts.empty(), "decoded scripts are not empty");
    Ok &= Require(Stats.ScriptCount == Program.Scripts.size(), "script count is preserved");
    Ok &= Require(Stats.AnimationEntryCount == ExpectedAnimationEntries,
                  "all script animation entries are emitted");
    Ok &= Require(Stats.ProgramWordCount == Program.ProgramData.size(),
                  "program word count is preserved");
    Ok &= Require(Stats.InstructionCount > 0, "instructions are emitted");
    Ok &= Require(Text.find("interface IScriptHost") != std::string::npos,
                  "host interface is emitted");
    Ok &= Require(Text.find("class IScriptState") != std::string::npos,
                  "script state is emitted");
    Ok &= Require(Text.find("bool TickScript") != std::string::npos,
                  "tick function is emitted");
    Ok &= Require(Text.find("switch (state.pc)") != std::string::npos,
                  "program counter state machine is emitted");
    Ok &= Require(Text.find("UnknownOpcode") != std::string::npos,
                  "unsupported opcodes remain explicit");

    return Ok ? 0 : 1;
}
