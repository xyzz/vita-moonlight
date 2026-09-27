#include "FrontendMode.hpp"

#include <cstdio>

#if defined(__PSV__)
#include <psp2/appmgr.h>
#endif

namespace moonlight
{

namespace
{

#if defined(__PSV__)
    constexpr const char* FRONTEND_CONFIG_PATH = "ux0:data/moonlight/interface.conf";
#endif

    const char* frontendName(FrontendMode mode)
    {
        return mode == FrontendMode::Legacy ? "legacy" : "borealis";
    }

}

bool saveFrontendMode(FrontendMode mode)
{
#if defined(__PSV__)
    FILE* file = std::fopen(FRONTEND_CONFIG_PATH, "w");
    if (!file)
        return false;

    const int writeResult = std::fprintf(file, "%s\n", frontendName(mode));
    const int closeResult = std::fclose(file);
    return writeResult > 0 && closeResult == 0;
#else
    (void)mode;
    return false;
#endif
}

bool restartWithFrontend(FrontendMode mode)
{
#if defined(__PSV__)
    if (!saveFrontendMode(mode))
        return false;
    return sceAppMgrLoadExec("app0:eboot.bin", nullptr, nullptr) >= 0;
#else
    (void)mode;
    return false;
#endif
}

}
