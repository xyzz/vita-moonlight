#pragma once
#include <stdarg.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C"
{
#endif

    // Backend logging primitive that writes to console/file. Uses printf-style formatting.
    __attribute__((format(printf, 1, 2))) void vita_debug_log(const char* fmt, ...);
    void vita_debug_log_raw(const char* text);

    // Enable or disable file logging (creates/truncates moonlight.log in config dir)
    void enable_file_logging(bool enable);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace vita_log
{

inline void vlog(const char* level, const char* fmt, va_list args)
{
    char buffer[1024];
    size_t level_length = strlen(level);
    if (level_length + 3 >= sizeof(buffer))
        return;
    buffer[0] = '[';
    memcpy(buffer + 1, level, level_length);
    buffer[level_length + 1] = ']';
    buffer[level_length + 2] = ' ';
    size_t prefix_length     = level_length + 3;
    va_list copy;
    va_copy(copy, args);
    int n = vsnprintf(buffer + prefix_length, sizeof(buffer) - prefix_length, fmt, copy);
    va_end(copy);
    if (n < 0)
        return;
    buffer[sizeof(buffer) - 1] = '\0';
    vita_debug_log_raw(buffer);
}

inline void info(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vlog("INFO", fmt, args);
    va_end(args);
}
inline void warning(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vlog("WARNING", fmt, args);
    va_end(args);
}
inline void error(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vlog("ERROR", fmt, args);
    va_end(args);
}
inline void debug(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vlog("DEBUG", fmt, args);
    va_end(args);
}
inline void verbose(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vlog("VERBOSE", fmt, args);
    va_end(args);
}

} // namespace vita_log
#endif

// Size printing compatibility for platforms without "%zu" support (PSVita)
#if defined(__PSP2__) || defined(VITA) || defined(__VITA__) || defined(PSVITA) || defined(__PSVITA__) || defined(__vita__)
#define FMT_SIZE_T "%lu"
#define FMT_SIZE_CAST(x) ((unsigned long)(x))
#else
#define FMT_SIZE_T "%zu"
#define FMT_SIZE_CAST(x) (x)
#endif
