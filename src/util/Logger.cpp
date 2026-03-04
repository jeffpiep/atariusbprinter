#include "util/Logger.h"
#include <cstdarg>
#include <cstdio>

#ifdef PLATFORM_LINUX
#include <chrono>
#elif defined(PLATFORM_RP2040)
#include "pico/time.h"
#endif

LogLevel Logger::s_level = LogLevel::INFO;

void Logger::setLevel(LogLevel level) {
    s_level = level;
}

LogLevel Logger::getLevel() {
    return s_level;
}

static const char* levelName(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
    }
    return "?    ";
}

static uint32_t timestampMs() {
#ifdef PLATFORM_LINUX
    using namespace std::chrono;
    static auto start = steady_clock::now();
    return static_cast<uint32_t>(
        duration_cast<milliseconds>(steady_clock::now() - start).count());
#elif defined(PLATFORM_RP2040)
    return to_ms_since_boot(get_absolute_time());
#else
    return 0;
#endif
}

void Logger::log(LogLevel level, const char* component, const char* fmt, va_list args) {
    if (level < s_level) return;

    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, args);

    uint32_t ts = timestampMs();

#ifdef PLATFORM_LINUX
    fprintf(stderr, "[%s][%7u ms][%-16s] %s\n", levelName(level), ts, component, buf);
#else
    printf("[%s][%7u ms][%-16s] %s\n", levelName(level), (unsigned)ts, component, buf);
#endif
}

void Logger::debug(const char* component, const char* fmt, ...) {
#ifdef NDEBUG
    (void)component; (void)fmt;
#else
    va_list args;
    va_start(args, fmt);
    log(LogLevel::DEBUG, component, fmt, args);
    va_end(args);
#endif
}

void Logger::info(const char* component, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log(LogLevel::INFO, component, fmt, args);
    va_end(args);
}

void Logger::warn(const char* component, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log(LogLevel::WARN, component, fmt, args);
    va_end(args);
}

void Logger::error(const char* component, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log(LogLevel::ERROR, component, fmt, args);
    va_end(args);
}
