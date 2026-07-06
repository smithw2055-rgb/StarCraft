#pragma once

#include <cstdlib>
#include <string>
#include <string_view>

#include "Core/Utils/Logger.h"

namespace Core::Utils
{

inline bool InputDebugEnabled()
{
    static const bool Enabled = []()
    {
        const char* Value = std::getenv("STARCRAFT_DEBUG_INPUT_COORDS");
        if (!Value || !Value[0])
            return false;

        const std::string_view Text(Value);
        return Text != "0" && Text != "false" && Text != "FALSE" &&
               Text != "off" && Text != "OFF";
    }();
    return Enabled;
}

inline bool InputDebugShouldLogMove(unsigned Period = 24)
{
    if (!InputDebugEnabled())
        return false;

    static unsigned Counter = 0;
    const bool ShouldLog = (Counter % (Period > 0 ? Period : 1)) == 0;
    ++Counter;
    return ShouldLog;
}

inline void InputDebugLog(std::string_view Tag, const std::string& Message)
{
    if (!InputDebugEnabled())
        return;
    Logger::Info(Tag, Message);
}

} // namespace Core::Utils
