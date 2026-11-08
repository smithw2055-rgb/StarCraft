#pragma once

#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace Core::Utils
{

enum class LogLevel
{
    Debug,
    Info,
    Warning,
    Error
};

class Logger
{
public:
    static void SetLevel(LogLevel Level)
    {
        CurrentLevel = Level;
    }

    static void Debug(std::string_view Tag, std::string_view Message)
    {
        Write(LogLevel::Debug, Tag, Message);
    }

    static void Info(std::string_view Tag, std::string_view Message)
    {
        Write(LogLevel::Info, Tag, Message);
    }

    static void Warning(std::string_view Tag, std::string_view Message)
    {
        Write(LogLevel::Warning, Tag, Message);
    }

    static void Error(std::string_view Tag, std::string_view Message)
    {
        Write(LogLevel::Error, Tag, Message);
    }

private:
    static void Write(LogLevel Level, std::string_view Tag, std::string_view Message)
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

        if (Level == LogLevel::Error)
            std::cerr << "[" << Stream.str() << "][" << LevelStr << "][" << Tag << "] " << Message << std::endl;
        else
            std::cout << "[" << Stream.str() << "][" << LevelStr << "][" << Tag << "] " << Message << std::endl;
    }

    inline static LogLevel CurrentLevel = LogLevel::Debug;
};

} // namespace Core::Utils
