#pragma once

#include "Game/IScriptVM.h"

#include <cstddef>
#include <iosfwd>
#include <string>

namespace Game
{

struct IScriptAngelScriptExportStats
{
    size_t ScriptCount = 0;
    size_t AnimationEntryCount = 0;
    size_t InstructionCount = 0;
    size_t ProgramWordCount = 0;
};

// Writes a self-contained AngelScript source module for a decoded IScript
// program. The generated module exposes an IScriptHost interface for engine
// side effects and keeps the current native VM as the runtime reference.
bool ExportIScriptToAngelScript(const IScriptProgram& Program,
                               std::ostream& Output,
                               IScriptAngelScriptExportStats* Stats = nullptr,
                               std::string* Error = nullptr);

} // namespace Game
