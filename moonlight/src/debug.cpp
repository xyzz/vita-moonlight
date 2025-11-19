#include "debug.hpp"

#include <stdarg.h>
#include <stdio.h>
#if defined(__PSV__)
#include <psp2/kernel/clib.h>
#endif

// Incluir para acceder a g_debug_log_enabled
#include "video/legacy/modules/vita_globals.hpp"

// Implementación para redirigir logs de libgamestream en Vita
extern "C" void vita_debug_log(const char* fmt, ...)
{
    // Solo imprimir si el debug log está habilitado
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
    // Asegurar newline para separar entradas (el código de llamadas a veces concatena)
    bool need_newline = (n == 0) || (buffer[n - 1] != '\n');
#if defined(__PSV__)
    sceClibPrintf("%s%s", buffer, need_newline ? "\n" : "");
#else
    fprintf(stdout, "%s%s", buffer, need_newline ? "\n" : "");
    fflush(stdout);
#endif
}
