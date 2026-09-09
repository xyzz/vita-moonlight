#include "utils/vita_platform.hpp"

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#ifdef BOREALIS_USE_GXM
#include <borealis/extern/nanovg/nanovg_gxm_utils.h>
#endif

namespace vita
{

std::uint64_t monotonicMicroseconds()
{
    return sceKernelGetSystemTimeWide();
}

std::uint64_t monotonicMilliseconds()
{
    return monotonicMicroseconds() / 1000ULL;
}

void delayBeforeGxmTeardown()
{
#ifdef BOREALIS_USE_GXM
    NVGXMwindow* window = gxmGetWindow();
    if (window && window->context)
        sceKernelDelayThread(1000);
#endif
}

}
