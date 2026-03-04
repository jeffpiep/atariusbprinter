#pragma once
#include <cstdarg>
#include <cstdint>

enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3 };

class Logger {
public:
    static void setLevel(LogLevel level);
    static LogLevel getLevel();

    static void debug(const char* component, const char* fmt, ...);
    static void info(const char* component, const char* fmt, ...);
    static void warn(const char* component, const char* fmt, ...);
    static void error(const char* component, const char* fmt, ...);

private:
    static void log(LogLevel level, const char* component, const char* fmt, va_list args);
    static LogLevel s_level;
};

// Convenience macros
#define LOG_DEBUG(comp, ...) Logger::debug(comp, __VA_ARGS__)
#define LOG_INFO(comp, ...)  Logger::info(comp,  __VA_ARGS__)
#define LOG_WARN(comp, ...)  Logger::warn(comp,  __VA_ARGS__)
#define LOG_ERROR(comp, ...) Logger::error(comp, __VA_ARGS__)
