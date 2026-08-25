# ENet is provided by moonlight-common-c.
# moonlight-common-c uses the crypto backend selected by the root project.
if(CMAKE_SYSTEM_NAME STREQUAL "Vita" OR PLATFORM_PSV)
    if(DEFINED BUILD_SHARED_LIBS)
        set(_MOONLIGHT_BUILD_SHARED_LIBS_WAS_DEFINED TRUE)
        set(_MOONLIGHT_BUILD_SHARED_LIBS_SAVED "${BUILD_SHARED_LIBS}")
    else()
        set(_MOONLIGHT_BUILD_SHARED_LIBS_WAS_DEFINED FALSE)
    endif()
    set(BUILD_SHARED_LIBS OFF)
endif()

add_subdirectory(moonlight-common-c)

if(CMAKE_SYSTEM_NAME STREQUAL "Vita" OR PLATFORM_PSV)
    if(_MOONLIGHT_BUILD_SHARED_LIBS_WAS_DEFINED)
        set(BUILD_SHARED_LIBS "${_MOONLIGHT_BUILD_SHARED_LIBS_SAVED}")
    else()
        unset(BUILD_SHARED_LIBS)
    endif()
    unset(_MOONLIGHT_BUILD_SHARED_LIBS_WAS_DEFINED)
    unset(_MOONLIGHT_BUILD_SHARED_LIBS_SAVED)
endif()


# h264bitstream static library
add_library(h264bitstream STATIC
    h264bitstream/h264_nal.c
    h264bitstream/h264_sei.c
    h264bitstream/h264_stream.c
)
target_include_directories(h264bitstream PUBLIC h264bitstream)

# inih static library
add_library(inih STATIC
    inih/ini.c
)
target_include_directories(inih PUBLIC inih)

add_subdirectory("${BOREALIS_LIBRARY}" "${CMAKE_CURRENT_BINARY_DIR}/borealis")

# gamestream static library
add_subdirectory(gamestream)

# Only the Vita client library belongs in this build. Host tools are built from
# the moonmic project itself.
set(BUILD_CLIENT ON CACHE BOOL "Build Moonmic client library" FORCE)
set(BUILD_HOST OFF CACHE BOOL "Build Moonmic host application" FORCE)
add_subdirectory(moonmic)

# moonbeam - Connection diagnostic tester library
add_subdirectory(moonbeam)

# mdnsniff portable library
add_library(mdnsniff STATIC
    mdnsniff/udp_sniffer_vita.c
    mdnsniff/udp_sniffer_win.c
    mdnsniff/common/debugScreen.c
)
target_include_directories(mdnsniff PUBLIC mdnsniff mdnsniff/common)

# Link Vita stubs only for Vita builds.
if(CMAKE_SYSTEM_NAME STREQUAL "Vita" OR PLATFORM_PSV)
    target_link_libraries(mdnsniff PUBLIC SceSysmodule_stub SceNet_stub SceNetCtl_stub SceCtrl_stub SceDisplay_stub)
endif()
