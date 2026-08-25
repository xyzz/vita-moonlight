cmake_minimum_required(VERSION 3.10)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Generate compile_commands.json for LSPs" FORCE)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PrepareBorealis.cmake)
prepare_borealis(MOONLIGHT_BOREALIS_SOURCE
    SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/borealis"
    PATCH "${CMAKE_CURRENT_SOURCE_DIR}/patches/borealis/psv-low-latency.patch"
)
set(BOREALIS_LIBRARY "${MOONLIGHT_BOREALIS_SOURCE}/library")

include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)

option(USE_SHARED_LIB "Whether to use shared libs provided by system" OFF)
cmake_dependent_option(USE_SYSTEM_FMT "" OFF "NOT USE_SHARED_LIB" ON)
cmake_dependent_option(USE_SYSTEM_TINYXML2 "" OFF "NOT USE_SHARED_LIB" ON)
cmake_dependent_option(USE_SYSTEM_TWEENY "" OFF "NOT USE_SHARED_LIB" ON)

include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

project(moonlight)

# Keep editor tooling independent of the active host-specific build directory.
add_custom_target(export-compile-commands ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
    COMMENT "Copying compile_commands.json to the repository root"
)

if(WIN32)
  set(OPENSSL_ROOT_DIR "${CMAKE_SOURCE_DIR}/openssl-mingw" CACHE PATH "" FORCE)
  set(OPENSSL_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/openssl-mingw/include" CACHE PATH "" FORCE)
  set(OPENSSL_CRYPTO_LIBRARY "${CMAKE_SOURCE_DIR}/openssl-mingw/lib/libcrypto.a" CACHE FILEPATH "" FORCE)
  set(OPENSSL_SSL_LIBRARY "${CMAKE_SOURCE_DIR}/openssl-mingw/lib/libssl.a" CACHE FILEPATH "" FORCE)
  set(OPENSSL_LIBRARIES "${OPENSSL_CRYPTO_LIBRARY};${OPENSSL_SSL_LIBRARY}" CACHE STRING "" FORCE)
elseif(PLATFORM_PSV)
  # VitaSDK provides OpenSSL for dependencies such as curl.
  set(OPENSSL_ROOT_DIR "$ENV{VITASDK}/arm-vita-eabi" CACHE PATH "" FORCE)
  set(OPENSSL_INCLUDE_DIR "$ENV{VITASDK}/arm-vita-eabi/include" CACHE PATH "" FORCE)
  set(OPENSSL_CRYPTO_LIBRARY "$ENV{VITASDK}/arm-vita-eabi/lib/libcrypto.a" CACHE FILEPATH "" FORCE)
  set(OPENSSL_SSL_LIBRARY "$ENV{VITASDK}/arm-vita-eabi/lib/libssl.a" CACHE FILEPATH "" FORCE)
  set(OPENSSL_LIBRARIES "${OPENSSL_CRYPTO_LIBRARY};${OPENSSL_SSL_LIBRARY}" CACHE STRING "" FORCE)
endif()
set(TITLE "Moonlight")
set(VERSION_MAJOR "1")
set(VERSION_MINOR "0")
set(VERSION_ALTER "0")
set(VERSION_BUILD "0")
set(PSN_TITLE_ID  "MNLHTVITA")
set(PSN_VERSION  "01.00")
set(PROJECT_AUTHOR "AorsiniYT")
set(PROJECT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/img/icon.jpg)
set(PROJECT_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/resources)

if (USE_LIBROMFS)
    add_libromfs(${PROJECT_NAME} ${PROJECT_RESOURCES})
endif ()

set(USE_MBEDTLS ON CACHE BOOL "Use MbedTLS instead of OpenSSL" FORCE)

add_subdirectory(third_party)

if(PLATFORM_PSV)
    if(TARGET fmt)
        message(STATUS "Applying PSV patch to fmt: forcing fallback file handling (no flockfile)")
        target_compile_definitions(fmt PRIVATE FMT_USE_FALLBACK_FILE=1)
    endif()
endif()

find_package(Threads REQUIRED)
list(APPEND APP_PLATFORM_LIB ${CMAKE_THREAD_LIBS_INIT})

list(APPEND APP_PLATFORM_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/moonlight/include)
list(APPEND APP_PLATFORM_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/moonlight/src/video)

file(GLOB_RECURSE _ALL_SRC CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} moonlight/*.cpp)
set(MAIN_SRC)
foreach(_f IN LISTS _ALL_SRC)
    list(APPEND MAIN_SRC "${CMAKE_CURRENT_SOURCE_DIR}/${_f}")
endforeach()

if(PLATFORM_PSV)
    if(NOT EXISTS "$ENV{VITASDK}/arm-vita-eabi/lib/libavcodec.a")
        message(FATAL_ERROR "FFmpeg-vita is missing from the VitaSDK sysroot. Run scripts/prepare/install_all.sh first.")
    endif()
    message(STATUS "FFmpeg-vita found in the VitaSDK sysroot")
endif()
add_definitions(-DVITA_NETOPT_ENABLED)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/moonlight/src/video/legacy)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/moonlight/src/video/ffmpeg)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/moonlight/src/video/ffmpeg/modules)

if (PLATFORM_PSV AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(PSV_COMPAT_SRC ${CMAKE_BINARY_DIR}/psv_compat_stdio.c)
    file(WRITE ${PSV_COMPAT_SRC} "/* Auto-generated stub for PSV: flockfile/funlockfile (Debug only) */\n#include <stdio.h>\nvoid flockfile(FILE* f) { (void)f; }\nvoid funlockfile(FILE* f) { (void)f; }\n")
    list(APPEND MAIN_SRC ${PSV_COMPAT_SRC})
endif()

set(PLATFORM_OPTION)
if (WIN32)
    configure_file("${CMAKE_SOURCE_DIR}/moonlight/resource.rc.in" "${CMAKE_BINARY_DIR}/resource.rc")
    list(APPEND MAIN_SRC ${CMAKE_BINARY_DIR}/resource.rc)
    if (MINGW)
        set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++ -static" CACHE STRING "" FORCE)
    endif ()
elseif (PLATFORM_PSV)
    add_definitions(-D__vita__ -D__PSV__)
    add_definitions(-DUSE_MBEDTLS_CRYPTO)
endif ()

if(WIN32)
    include_directories(SYSTEM /usr/x86_64-w64-mingw32/include)
    link_directories(/usr/x86_64-w64-mingw32/lib)
    include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/openssl-mingw/include)
    link_directories(${CMAKE_SOURCE_DIR}/openssl-mingw/lib)
    add_definitions(-DOPENSSL_NO_SECURE_MEMORY)
elseif(PLATFORM_PSV)
    # The Vita GCC toolchain already adds arm-vita-eabi/include after the
    # libstdc++ headers. Re-adding the same directory with -isystem moves it
    # ahead of libstdc++'s #include_next chain; GCC 15 then fails to resolve
    # headers such as <stdlib.h> from <cstdlib>.
    link_directories($ENV{VITASDK}/arm-vita-eabi/lib)
endif()

program_target(${PROJECT_NAME} "${MAIN_SRC}")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)

target_compile_definitions(${PROJECT_NAME} PRIVATE USE_MBEDTLS)

if (PLATFORM_PSV)
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,--wrap=LiRequestIdrFrame -Wl,-z,noexecstack -Wl,--allow-multiple-definition)
    target_compile_options(${PROJECT_NAME} PRIVATE -pthread)
endif()

if (WIN32)
    add_custom_target(${PROJECT_NAME}.data
            COMMAND "${CMAKE_COMMAND}" -E copy_directory ${PROJECT_RESOURCES} ${CMAKE_CURRENT_BINARY_DIR}/resources
    )
    if (NOT USE_LIBROMFS)
        add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}.data)
    endif ()
elseif (PLATFORM_PSV)
    set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d ATTRIBUTE2=12") # max heap size mode
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.velf
        COMMAND vita-elf-create ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME} ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.velf
        DEPENDS ${PROJECT_NAME}
        COMMENT "Creating ${PROJECT_NAME}.velf"
    )
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.self
        COMMAND vita-make-fself -a 0x2800000000000001 -at 0x0E -c ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.velf ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.self
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.velf
        COMMENT "Creating ${PROJECT_NAME}.self"
    )
    add_custom_target(${PROJECT_NAME}_self_target ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.self)
    set(PSV_ASSETS_FILES ${CMAKE_SOURCE_DIR}/psv/sce_sys sce_sys)
    if (NOT USE_LIBROMFS)
        list(APPEND PSV_ASSETS_FILES ${CMAKE_SOURCE_DIR}/resources resources)
    endif ()
    if (NOT USE_GXM)
        list(APPEND PSV_ASSETS_FILES ${CMAKE_SOURCE_DIR}/psv/module/ module)
    endif ()
    if (USE_VITA_SHARK)
        list(APPEND PSV_ASSETS_FILES "${CMAKE_BINARY_DIR}/vendor/SceShaccCg" module)
    endif ()
    vita_create_vpk(${PROJECT_NAME}.vpk ${PSN_TITLE_ID} ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.self
            VERSION ${PSN_VERSION}
            NAME ${TITLE}
            FILE ${PSV_ASSETS_FILES}
    )
    if(TARGET ${PROJECT_NAME}.vpk-vpk)
        add_dependencies(${PROJECT_NAME}.vpk-vpk ${PROJECT_NAME}_self_target)
    else()
        add_dependencies(${PROJECT_NAME}.vpk ${PROJECT_NAME}_self_target)
    endif()
endif ()

target_include_directories(${PROJECT_NAME} PRIVATE moonlight ${APP_PLATFORM_INCLUDE} ${CMAKE_SOURCE_DIR}/third_party ${CMAKE_SOURCE_DIR}/third_party/moonlight-common-c/reedsolomon ${CMAKE_SOURCE_DIR}/third_party/moonlight-common-c/src)

target_include_directories(${PROJECT_NAME} PRIVATE
    moonlight/include
    moonlight/src
    moonlight/src/crypto
    third_party/
)
target_compile_options(${PROJECT_NAME} PRIVATE -ffunction-sections -fdata-sections ${APP_PLATFORM_OPTION})
target_link_libraries(${PROJECT_NAME} PRIVATE borealis ${APP_PLATFORM_LIB} inih mdnsniff gamestream moonmic moonbeam)

if(PLATFORM_PSV)
    # The sysroot curl package uses OpenSSL, while the application crypto
    # backend uses mbedTLS.
    set(CRYPTO_LIBS -lssl -lcrypto)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        -lSceVshBridge_stub
        -lexpat
        -lcurl
        -lzstd
        -lz
        ${CRYPTO_LIBS}
        -lopus
        -lbz2
        -Wl,--whole-archive -lpthread -Wl,--no-whole-archive
        -lSceDisplay_stub
        -lSceCtrl_stub
        -lSceTouch_stub
        -lSceNet_stub
        -lSceNetCtl_stub
        -lScePower_stub
        -lSceVideodec_stub
        -lSceCommonDialog_stub
        -lSceAppMgr_stub
        -lSceAudio_stub
        -lSceAudioIn_stub
        -lSceIme_stub
        -lSceAppUtil_stub
        -lSceMotion_stub
        -lSceShellSvc_stub
        -lSceGxm_stub
        -lScePgf_stub
        -lvita2d
        -lfreetype
        -lpng
        -ljpeg
        -lc
        inih
        mdnsniff
        enet
        h264bitstream
        moonlight-common-c
    )

    if (EXISTS "$ENV{VITASDK}/arm-vita-eabi/lib/libmbedtls.a")
        message(STATUS "mbedTLS static libs found in VITASDK sysroot; linking mbedtls, mbedx509 and mbedcrypto")
        target_link_libraries(${PROJECT_NAME} PRIVATE
            "$ENV{VITASDK}/arm-vita-eabi/lib/libmbedtls.a"
            "$ENV{VITASDK}/arm-vita-eabi/lib/libmbedx509.a"
            "$ENV{VITASDK}/arm-vita-eabi/lib/libmbedcrypto.a"
        )
    elseif (TARGET MbedTLS::lib)
        target_link_libraries(${PROJECT_NAME} PRIVATE MbedTLS::lib)
    elseif (EXISTS "$ENV{VITASDK}/arm-vita-eabi/lib/libmbedcrypto.a")
        target_link_libraries(${PROJECT_NAME} PRIVATE -lmbedtls -lmbedx509 -lmbedcrypto)
    endif()
    if (EXISTS "$ENV{VITASDK}/arm-vita-eabi/lib/libavcodec.a")
        message(STATUS "Linking FFmpeg libs from VITASDK sysroot (absolute paths)")
        set(FFMPEG_LIB_DIR "$ENV{VITASDK}/arm-vita-eabi/lib")
        set(FFMPEG_LIBS
            "${FFMPEG_LIB_DIR}/libavcodec.a"
            "${FFMPEG_LIB_DIR}/libavformat.a"
            "${FFMPEG_LIB_DIR}/libavutil.a"
            "${FFMPEG_LIB_DIR}/libswscale.a"
            "${FFMPEG_LIB_DIR}/libswresample.a"
        )
        if (EXISTS "${FFMPEG_LIB_DIR}/libz.a")
            list(APPEND FFMPEG_LIBS "${FFMPEG_LIB_DIR}/libz.a")
        else()
            list(APPEND FFMPEG_LIBS z)
        endif()
        if (EXISTS "${FFMPEG_LIB_DIR}/liblzma.a")
            list(APPEND FFMPEG_LIBS "${FFMPEG_LIB_DIR}/liblzma.a")
        endif()
        list(APPEND FFMPEG_LIBS pthread)
        list(APPEND FFMPEG_LIBS
            SceAvcodec_stub
            SceAudiodec_stub
            SceCodecEngine_stub
            SceVideodec_stub
        )

        target_link_libraries(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBS})
    endif()
endif()

if(WIN32)
    target_link_directories(${PROJECT_NAME} PRIVATE /usr/x86_64-w64-mingw32/lib)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        borealis
        ${APP_PLATFORM_LIB}
        inih
        mdnsniff
        enet
        h264bitstream
        moonlight-common-c
        z
        ws2_32
        crypt32
        iphlpapi
        winmm
        bcrypt
        rpcrt4
        user32
        gdi32
        advapi32
        shell32
        /usr/x86_64-w64-mingw32/lib/libexpat.dll.a
        ${CMAKE_SOURCE_DIR}/openssl-mingw/lib/libssl.a
        ${CMAKE_SOURCE_DIR}/openssl-mingw/lib/libcrypto.a
        ${CMAKE_SOURCE_DIR}/openssl-mingw/lib/libcurl.dll.a
    )
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/openssl-mingw/bin/libssl-1_1-x64.dll"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/openssl-mingw/bin/libcrypto-1_1-x64.dll"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/openssl-mingw/bin/libcurl-4.dll"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "/usr/x86_64-w64-mingw32/lib/libwinpthread-1.dll"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "/usr/x86_64-w64-mingw32/bin/libexpat-1.dll"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/third_party/moonlight-common-c/libmoonlight-common-c.dll"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
endif()
