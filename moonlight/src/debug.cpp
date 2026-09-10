#include "debug.hpp"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>

#if defined(__PSV__)
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include <mutex>
#endif

#include <sys/stat.h>

#include "ConfigManager.hpp"

static FILE* g_moonlight_log_file = nullptr;
static std::atomic<bool> g_debug_log_enabled { false };

#if defined(__PSV__)
namespace
{

constexpr size_t LOG_QUEUE_CAPACITY      = 64;
constexpr size_t LOG_MESSAGE_SIZE        = 1026;
constexpr int BACKGROUND_THREAD_PRIORITY = 0x10000114;

struct AsyncLogMessage
{
    char text[LOG_MESSAGE_SIZE];
};

AsyncLogMessage g_log_queue[LOG_QUEUE_CAPACITY];
size_t g_log_queue_read  = 0;
size_t g_log_queue_write = 0;
size_t g_log_queue_count = 0;
std::mutex g_log_queue_mutex;
std::mutex g_log_lifecycle_mutex;
std::atomic<SceUID> g_log_sema { -1 };
std::atomic<SceUID> g_log_thread { -1 };
std::atomic<bool> g_log_running { false };
std::atomic<bool> g_log_stop { false };
std::atomic<uint32_t> g_log_dropped { 0 };
bool g_log_atexit_registered = false;
char g_log_file_buffer[64 * 1024];

void write_log_message(const char* text)
{
    // NetDbgLogPc receives only the sceClibPrintf stream.
    sceClibPrintf("%s", text);
    if (g_moonlight_log_file)
    {
        std::fputs(text, g_moonlight_log_file);
    }
}

bool pop_log_message(AsyncLogMessage* message)
{
    std::lock_guard<std::mutex> lock(g_log_queue_mutex);
    if (g_log_queue_count == 0)
    {
        return false;
    }

    *message         = g_log_queue[g_log_queue_read];
    g_log_queue_read = (g_log_queue_read + 1) % LOG_QUEUE_CAPACITY;
    g_log_queue_count--;
    return true;
}

int log_thread_main(SceSize, void*)
{
    while (true)
    {
        SceUID sema = g_log_sema.load(std::memory_order_acquire);
        if (sema < 0)
        {
            break;
        }

        SceUInt timeout = 2000000;
        sceKernelWaitSema(sema, 1, &timeout);

        AsyncLogMessage message;
        bool wroteMessage = false;
        while (pop_log_message(&message))
        {
            write_log_message(message.text);
            wroteMessage = true;
        }

        uint32_t dropped = g_log_dropped.exchange(0, std::memory_order_acq_rel);
        if (dropped)
        {
            char warning[128];
            snprintf(warning, sizeof(warning), "[WARNING] [Logger] dropped %u messages\n", dropped);
            write_log_message(warning);
            wroteMessage = true;
        }

        if (g_moonlight_log_file && (g_log_stop.load(std::memory_order_acquire) || !wroteMessage))
        {
            std::fflush(g_moonlight_log_file);
        }

        if (g_log_stop.load(std::memory_order_acquire))
        {
            std::lock_guard<std::mutex> lock(g_log_queue_mutex);
            if (g_log_queue_count == 0)
            {
                break;
            }
        }
    }

    if (g_moonlight_log_file)
    {
        std::fflush(g_moonlight_log_file);
    }
    return 0;
}

void stop_async_logger();

void start_async_logger()
{
    std::lock_guard<std::mutex> lifecycleLock(g_log_lifecycle_mutex);
    if (g_log_running.load(std::memory_order_acquire))
    {
        return;
    }

    SceUID sema = sceKernelCreateSema("moonlight_log_queue", 0, 0, 1, nullptr);
    if (sema < 0)
    {
        sceClibPrintf("[Logger] sceKernelCreateSema failed: 0x%08X\n", sema);
        return;
    }

    SceUID thread = sceKernelCreateThread("moonlight_log", log_thread_main,
        BACKGROUND_THREAD_PRIORITY, 0x8000, 0, 0, nullptr);
    if (thread < 0)
    {
        sceClibPrintf("[Logger] sceKernelCreateThread failed: 0x%08X\n", thread);
        sceKernelDeleteSema(sema);
        return;
    }

    g_log_stop.store(false, std::memory_order_release);
    g_log_sema.store(sema, std::memory_order_release);
    g_log_thread.store(thread, std::memory_order_release);

    int startResult = sceKernelStartThread(thread, 0, nullptr);
    if (startResult < 0)
    {
        sceClibPrintf("[Logger] sceKernelStartThread failed: 0x%08X\n", startResult);
        g_log_thread.store(-1, std::memory_order_release);
        g_log_sema.store(-1, std::memory_order_release);
        sceKernelDeleteThread(thread);
        sceKernelDeleteSema(sema);
        return;
    }

    g_log_running.store(true, std::memory_order_release);
    if (!g_log_atexit_registered)
    {
        atexit(stop_async_logger);
        g_log_atexit_registered = true;
    }
}

void stop_async_logger()
{
    std::lock_guard<std::mutex> lifecycleLock(g_log_lifecycle_mutex);
    if (!g_log_running.load(std::memory_order_acquire))
    {
        return;
    }

    g_log_stop.store(true, std::memory_order_release);
    SceUID sema = g_log_sema.load(std::memory_order_acquire);
    if (sema >= 0)
    {
        sceKernelSignalSema(sema, 1);
    }

    SceUID thread = g_log_thread.load(std::memory_order_acquire);
    if (thread >= 0)
    {
        sceKernelWaitThreadEnd(thread, nullptr, nullptr);
        sceKernelDeleteThread(thread);
    }
    if (sema >= 0)
    {
        sceKernelDeleteSema(sema);
    }

    g_log_thread.store(-1, std::memory_order_release);
    g_log_sema.store(-1, std::memory_order_release);
    g_log_running.store(false, std::memory_order_release);
    g_log_stop.store(false, std::memory_order_release);
}

bool enqueue_log_message(const char* text)
{
    std::unique_lock<std::mutex> lock(g_log_queue_mutex, std::try_to_lock);
    if (!lock.owns_lock() || g_log_queue_count == LOG_QUEUE_CAPACITY)
    {
        g_log_dropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    strncpy(g_log_queue[g_log_queue_write].text, text, LOG_MESSAGE_SIZE - 1);
    g_log_queue[g_log_queue_write].text[LOG_MESSAGE_SIZE - 1] = '\0';
    g_log_queue_write                                         = (g_log_queue_write + 1) % LOG_QUEUE_CAPACITY;
    g_log_queue_count++;
    lock.unlock();

    SceUID sema = g_log_sema.load(std::memory_order_acquire);
    if (sema >= 0)
    {
        sceKernelSignalSema(sema, 1);
    }
    return true;
}

} // namespace
#endif

void enable_file_logging(bool enable)
{
    g_debug_log_enabled.store(enable, std::memory_order_release);
    if (enable)
    {
        if (g_moonlight_log_file)
            return; // already enabled
        std::string cfgPath = ConfigManager::getConfigPath();
        size_t p            = cfgPath.find_last_of("/\\");
        std::string cfgDir  = (p != std::string::npos) ? cfgPath.substr(0, p) : ".";
        std::string logPath = cfgDir + "/moonlight.log";
#if defined(__PSV__)
        FILE* f = std::fopen(logPath.c_str(), "w+");
        if (!f)
            return;
        g_moonlight_log_file = f;
        setvbuf(g_moonlight_log_file, g_log_file_buffer, _IOFBF, sizeof(g_log_file_buffer));
#elif defined(_WIN32)
        FILE* f = std::fopen(logPath.c_str(), "w+");
        if (!f)
            return;
        g_moonlight_log_file = f;
        setvbuf(g_moonlight_log_file, nullptr, _IOFBF, 64 * 1024);
#else
        FILE* f = std::fopen(logPath.c_str(), "w+");
        if (!f)
            return;
        g_moonlight_log_file = f;
        setvbuf(g_moonlight_log_file, nullptr, _IOLBF, 0);
#endif
#if defined(__PSV__)
        start_async_logger();
#endif
    }
    else
    {
#if defined(__PSV__)
        stop_async_logger();
#endif
        if (g_moonlight_log_file)
        {
            std::fflush(g_moonlight_log_file);
            std::fclose(g_moonlight_log_file);
            g_moonlight_log_file = nullptr;
        }
    }
}

extern "C" void vita_debug_log_raw(const char* text)
{
    if (!g_debug_log_enabled.load(std::memory_order_acquire) || !text)
    {
        return;
    }

    // Ensure newline to separate entries (calling code sometimes concatenates)
    size_t length     = strnlen(text, 1024);
    bool need_newline = (length == 0) || (text[length - 1] != '\n');
#if defined(__PSV__)
    char message[1026];
    memcpy(message, text, length);
    if (need_newline && length < sizeof(message) - 1)
    {
        message[length++] = '\n';
    }
    message[length] = '\0';

    if (!g_log_running.load(std::memory_order_acquire))
    {
        start_async_logger();
    }
    if (g_log_running.load(std::memory_order_acquire))
    {
        enqueue_log_message(message);
    }
    else
    {
        write_log_message(message);
    }
#else
    fprintf(stdout, "%s%s", text, need_newline ? "\n" : "");
    fflush(stdout);
    if (g_moonlight_log_file)
    {
        std::fprintf(g_moonlight_log_file, "%s%s", text, need_newline ? "\n" : "");
        std::fflush(g_moonlight_log_file);
    }
#endif
}

// Implementation to redirect libgamestream logs on Vita
extern "C" void vita_debug_log(const char* fmt, ...)
{
    if (!g_debug_log_enabled.load(std::memory_order_acquire))
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
    vita_debug_log_raw(buffer);
}
