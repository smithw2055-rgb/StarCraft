#pragma once

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
    static void SetLevel(LogLevel Level);
    static void Debug(std::string_view Tag, std::string_view Message);
    static void Info(std::string_view Tag, std::string_view Message);
    static void Warning(std::string_view Tag, std::string_view Message);
    static void Error(std::string_view Tag, std::string_view Message);

private:
    static void Write(LogLevel Level, std::string_view Tag, std::string_view Message);
    static LogLevel CurrentLevel;
};

} // namespace Core::Utils
