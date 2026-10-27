#ifndef MDNS_LOG_H
#define MDNS_LOG_H

#ifdef __vita__
#include <psp2/kernel/clib.h>
#define MDNS_LOG(...) sceClibPrintf(__VA_ARGS__)
#else
#include <stdio.h>
#define MDNS_LOG(...) printf(__VA_ARGS__)
#endif

#define mdns_log MDNS_LOG

#endif // MDNS_LOG_H
