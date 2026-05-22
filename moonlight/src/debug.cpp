#include "debug.hpp"

#include <stdarg.h>
#include <stdio.h>
#if defined(__PSV__)
#include <psp2/kernel/clib.h>
#endif

// Include to access g_debug_log_enabled
#include <sys/stat.h>

#include "ConfigManager.hpp"
#include "video/legacy/modules/vita_globals.hpp"

// File logging state
static FILE* g_moonlight_log_file = nullptr;

void enable_file_logging(bool enable)
{
    if (enable)
    {
        if (g_moonlight_log_file)
            return; // already enabled
        std::string cfgPath = ConfigManager::getConfigPath();
        size_t p            = cfgPath.find_last_of("/\\");
        std::string cfgDir  = (p != std::string::npos) ? cfgPath.substr(0, p) : ".";
        std::string logPath = cfgDir + "/moonlight.log";
#ifdef _WIN32
        FILE* f = std::fopen(logPath.c_str(), "w+");
        if (!f)
            return;
        g_moonlight_log_file = f;
        setvbuf(g_moonlight_log_file, nullptr, _IOLBF, 0);
#else
        FILE* f = std::fopen(logPath.c_str(), "w+");
        if (!f)
            return;
        g_moonlight_log_file = f;
        setvbuf(g_moonlight_log_file, nullptr, _IOLBF, 0);
#endif
    }
    else
    {
        if (g_moonlight_log_file)
        {
            std::fflush(g_moonlight_log_file);
            std::fclose(g_moonlight_log_file);
            g_moonlight_log_file = nullptr;
        }
    }
}

// Implementation to redirect libgamestream logs on Vita
extern "C" void vita_debug_log(const char* fmt, ...)
{
    // Only print if debug log is enabled
    if (!g_debug_log_enabled)
    {
        return;
    }

    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buffer, sizeof(buffer) - 1, fmt, args);
    va_end(args);
    if (n < 0)
    {
        return;
    }
    buffer[sizeof(buffer) - 1] = '\0';
    // Ensure newline to separate entries (calling code sometimes concatenates)
    bool need_newline = (n == 0) || (buffer[n - 1] != '\n');
#if defined(__PSV__)
    sceClibPrintf("%s%s", buffer, need_newline ? "\n" : "");
    if (g_moonlight_log_file)
    {
        std::fprintf(g_moonlight_log_file, "%s%s", buffer, need_newline ? "\n" : "");
        std::fflush(g_moonlight_log_file);
    }
#else
    fprintf(stdout, "%s%s", buffer, need_newline ? "\n" : "");
    fflush(stdout);
    if (g_moonlight_log_file)
    {
        std::fprintf(g_moonlight_log_file, "%s%s", buffer, need_newline ? "\n" : "");
        std::fflush(g_moonlight_log_file);
    }
#endif
}
