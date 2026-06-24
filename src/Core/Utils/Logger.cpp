#include "Core/Utils/Logger.h"

#include <iostream>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace Core::Utils
{

LogLevel Logger::CurrentLevel = LogLevel::Debug;

void Logger::SetLevel(LogLevel Level)
{
    CurrentLevel = Level;
}

void Logger::Debug(std::string_view Tag, std::string_view Message)
{
    Write(LogLevel::Debug, Tag, Message);
}

void Logger::Info(std::string_view Tag, std::string_view Message)
{
    Write(LogLevel::Info, Tag, Message);
}

void Logger::Warning(std::string_view Tag, std::string_view Message)
{
    Write(LogLevel::Warning, Tag, Message);
}

void Logger::Error(std::string_view Tag, std::string_view Message)
{
    Write(LogLevel::Error, Tag, Message);
}

void Logger::Write(LogLevel Level, std::string_view Tag, std::string_view Message)
{
    if (Level < CurrentLevel)
        return;

    auto Now = std::time(nullptr);
    auto* Tm = std::localtime(&Now);

    std::ostringstream Stream;
    Stream << std::put_time(Tm, "%H:%M:%S");

    const char* LevelStr = "";
    switch (Level)
    {
    case LogLevel::Debug:   LevelStr = "DEBUG";   break;
    case LogLevel::Info:    LevelStr = "INFO";    break;
    case LogLevel::Warning: LevelStr = "WARN";    break;
    case LogLevel::Error:   LevelStr = "ERROR";   break;
    }

    // Colorize output
    if (Level == LogLevel::Error)
        std::cerr << "[" << Stream.str() << "][" << LevelStr << "][" << Tag << "] " << Message << std::endl;
    else
        std::cout << "[" << Stream.str() << "][" << LevelStr << "][" << Tag << "] " << Message << std::endl;
}

} // namespace Core::Utils
