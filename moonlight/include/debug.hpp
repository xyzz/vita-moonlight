#pragma once
#include <stdarg.h>
#ifdef __cplusplus
extern "C"
{
#endif
    // Log básico (siempre imprime). Usa formato printf.
    __attribute__((format(printf, 1, 2))) void vita_debug_log(const char* fmt, ...);
#ifdef __cplusplus
}
#endif

// Size printing compatibility for platforms without "%zu" support (PSVita)
#if defined(__PSP2__) || defined(VITA) || defined(__VITA__) || defined(PSVITA) || defined(__PSVITA__) || defined(__vita__)
#define FMT_SIZE_T "%lu"
#define FMT_SIZE_CAST(x) ((unsigned long)(x))
#else
#define FMT_SIZE_T "%zu"
#define FMT_SIZE_CAST(x) (x)
#endif
