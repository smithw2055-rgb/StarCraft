#include "Game/IScriptAngelScriptExporter.h"

#include <angelscript.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
    void MessageCallback(const asSMessageInfo* Message, void*)
    {
        if (Message->type == asMSGTYPE_ERROR)
            std::cerr << Message->section << ":" << Message->row << ":"
                      << Message->col << ": " << Message->message << "\n";
    }
}

int main()
{
    Game::IScriptDatabase Database;
    if (!Database.Load("assets/scripts/iscript.bin", "assets/arr/images.dat"))
    {
        std::cerr << "Failed to load IScript resources\n";
        return 1;
    }

    std::string Text;
    std::ifstream GeneratedFile("assets/scripts/iscript.as", std::ios::binary);
    if (GeneratedFile)
    {
        std::ostringstream Source;
        Source << GeneratedFile.rdbuf();
        Text = Source.str();
    }
    else
    {
        std::ostringstream Source;
        std::string Error;
        if (!Game::ExportIScriptToAngelScript(Database.GetProgram(), Source, nullptr, &Error))
        {
            std::cerr << "Failed to export AngelScript: " << Error << "\n";
            return 1;
        }
        Text = Source.str();
    }

    if (Text.empty())
    {
        std::cerr << "Generated AngelScript is empty\n";
        return 1;
    }

    asIScriptEngine* Engine = asCreateScriptEngine();
    if (!Engine)
    {
        std::cerr << "Failed to create AngelScript engine\n";
        return 1;
    }
    Engine->SetMessageCallback(asFUNCTION(MessageCallback), nullptr, asCALL_CDECL);

    asIScriptModule* Module = Engine->GetModule("iscript_export", asGM_ALWAYS_CREATE);
    const int AddResult = Module->AddScriptSection("iscript.as", Text.data(), Text.size());
    const int BuildResult = AddResult >= 0 ? Module->Build() : AddResult;
    Engine->ShutDownAndRelease();
    return BuildResult >= 0 ? 0 : 1;
}
