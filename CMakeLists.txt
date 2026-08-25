
cmake_minimum_required(VERSION 3.10)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Generate compile_commands.json for LSPs" )

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

## Soporte solo para Windows y PSVita

include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

project(moonlight)

# Asegurar que la exportación de compile_commands esté activada para LSPs
# (esto fuerza la variable activa en la configuración actual, no solo en la cache).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Copiar compile_commands.json al raíz del fuente para que editores/LSP lo
# encuentren sin necesidad de configurar rutas a la carpeta de build.
if(CMAKE_EXPORT_COMPILE_COMMANDS)
    add_custom_target(export-compile-commands ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/compile_commands.json
            ${CMAKE_SOURCE_DIR}/compile_commands.json
        COMMENT "Copiando compile_commands.json a la raíz del repo"
    )
endif()

## Forzar rutas de OpenSSL para MinGW/Windows y PS Vita (antes de cualquier add_subdirectory o find_package)
message(STATUS "CMAKE_CXX_COMPILER_ID: ${CMAKE_CXX_COMPILER_ID}")
if(WIN32)
  set(OPENSSL_ROOT_DIR "${CMAKE_SOURCE_DIR}/openssl-mingw" CACHE PATH "" FORCE)
  set(OPENSSL_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/openssl-mingw/include" CACHE PATH "" FORCE)
  set(OPENSSL_CRYPTO_LIBRARY "${CMAKE_SOURCE_DIR}/openssl-mingw/lib/libcrypto.a" CACHE FILEPATH "" FORCE)
  set(OPENSSL_SSL_LIBRARY "${CMAKE_SOURCE_DIR}/openssl-mingw/lib/libssl.a" CACHE FILEPATH "" FORCE)
  set(OPENSSL_LIBRARIES "${OPENSSL_CRYPTO_LIBRARY};${OPENSSL_SSL_LIBRARY}" CACHE STRING "" FORCE)
elseif(PLATFORM_PSV)
  # Para PS Vita, usar OpenSSL del sistema (igual que moonlight-legacy)
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

# Always use MbedTLS instead of OpenSSL
set(USE_MBEDTLS ON CACHE BOOL "Use MbedTLS instead of OpenSSL" FORCE)

add_subdirectory(third_party)

if(PLATFORM_PSV)
    if(TARGET fmt)
        message(STATUS "Applying PSV patch to fmt: forcing fallback file handling (no flockfile)")
        target_compile_definitions(fmt PRIVATE FMT_USE_FALLBACK_FILE=1)
    endif()
endif()

# Build ffmpeg module as an independent target and link it into the exe.
# Exclude its sources from the global MAIN_SRC glob below so we don't compile
# the same files twice into the executable.
# Note: ffmpeg sources are built as part of the global source tree now
# (we avoid internal CMakeLists under moonlight/src/video to simplify maintenance)

find_package(Threads REQUIRED)
list(APPEND APP_PLATFORM_LIB ${CMAKE_THREAD_LIBS_INIT})

# setting src and include

list(APPEND APP_PLATFORM_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/moonlight/include)
list(APPEND APP_PLATFORM_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/moonlight/src/video)

# Option to enable/disable building the bundled FFmpeg integration
set(BUILD_FFMPEG ON)

# Collect main sources: glob all and exclude ffmpeg subtree if BUILD_FFMPEG is OFF.
file(GLOB_RECURSE _ALL_SRC RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} moonlight/*.cpp)
set(MAIN_SRC)

# NOTE: wiliwili builds FFmpeg externally for PSVita and links precompiled
# static archives from the VITASDK sysroot. To avoid mixing in-tree FFmpeg
# sources with prebuilt FFmpeg archives (which may use different compile
# flags for ABI/FPU), exclude the FFmpeg subtree from the global source
# glob. When BUILD_FFMPEG is enabled we prefer linking the prebuilt
# archives under $VITASDK/arm-vita-eabi/lib; if those archives are missing
# the user must either install/build FFmpeg for Vita in the sysroot or
# disable BUILD_FFMPEG.
# Exclude nothing, compile everything including FFmpeg module

foreach(_f IN LISTS _ALL_SRC)
    list(APPEND MAIN_SRC "${CMAKE_CURRENT_SOURCE_DIR}/${_f}")
endforeach()

# If BUILD_FFMPEG is enabled, prefer linking prebuilt FFmpeg archives from the
# VITASDK sysroot. Warn if the archives are missing. Note: we still compile
# our integration glue (moonlight/src/video/ffmpeg/*.cpp) from source because
# those implement the `ffmpeg_video_*` functions used by the project.
if(NOT EXISTS "$ENV{VITASDK}/arm-vita-eabi/lib/libavcodec.a")
    message(WARNING "No FFmpeg static libs found in VITASDK sysroot; please install/build FFmpeg for Vita into $ENV{VITASDK}/arm-vita-eabi/lib.")
else()
    message(STATUS "Prebuilt FFmpeg libs found in VITASDK sysroot; linking against them.")
endif()
# Nota: NetworkOptimizations.cpp añadido en moonlight/src/network/ queda incluido por el GLOB anterior.
# Si se desea desactivar en runtime se expone vita_netopt_set_enabled().
add_definitions(-DVITA_NETOPT_ENABLED)
# vita.cpp se movió a video/legacy/vita.cpp y ya entra en el glob
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

# Configuración de curl y OpenSSL para MinGW/Windows y PS Vita
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

# Expose BUILD_FFMPEG macro to sources so we can guard FFmpeg-specific code
target_compile_definitions(${PROJECT_NAME} PRIVATE BUILD_FFMPEG=1 USE_MBEDTLS)

# Envolver LiRequestIdrFrame para redirigir a optimizaciones de red
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

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/third_party/inih)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/third_party/moonlight-common-c/reedsolomon)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/third_party/moonlight-common-c/src)

target_include_directories(${PROJECT_NAME} PRIVATE moonlight ${APP_PLATFORM_INCLUDE} ${CMAKE_SOURCE_DIR}/third_party ${CMAKE_SOURCE_DIR}/third_party/moonlight-common-c/reedsolomon ${CMAKE_SOURCE_DIR}/third_party/moonlight-common-c/src)

# Incluye los headers principales, crypto y gamestream para todo el proyecto
target_include_directories(${PROJECT_NAME} PUBLIC
    moonlight/include
    moonlight/src
    moonlight/src/crypto
    third_party/
)
target_compile_options(${PROJECT_NAME} PRIVATE -ffunction-sections -fdata-sections ${APP_PLATFORM_OPTION})
target_link_libraries(${PROJECT_NAME} PRIVATE borealis ${APP_PLATFORM_LIB} inih mdnsniff gamestream moonmic moonbeam)

if(PLATFORM_PSV)
    # Enlaza todas las librerías de PSVita necesarias (igual que moonlight-legacy)
    # curl del sysroot puede venir con backend OpenSSL (openssl.c.obj), incluso
    # cuando BUILD_FFMPEG=ON. Enlazamos ssl+crypto para cubrir ambos casos.
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
        -lz
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
    include_directories(
    include/
    moonlight/include
    moonlight/src
    third_party/moonlight-common-c/src/
    third_party/moonlight-common-c/reedsolomon/
    third_party/gamestream/
    third_party/h264bitstream/
    third_party/enet/include/
    third_party/inih/
    third_party/mdnsniff/
)
    # Si FFmpeg-vita está instalado en el sysroot, enlazar sus bibliotecas para
    # resolver símbolos usados por los sources FFmpeg que ahora se compilan
    # dentro del árbol global de fuentes.
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

# Enlace de librerías para Windows (sin expat/XML, usando inih, emparejamiento requiere OpenSSL)
if(WIN32)
 # Asegura que el linker busque en la ruta de MinGW para expat y otras libs
    target_link_directories(${PROJECT_NAME} PRIVATE /usr/x86_64-w64-mingw32/lib)
    #
    # El orden de las librerías es crítico en MinGW/Windows
    # Enlazamos contra los import libraries (.dll.a) de OpenSSL y curl
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
    # Copia las DLLs de OpenSSL y libcurl al directorio de salida tras compilar
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

# (La exportación de compile_commands.json ya se fuerza más arriba)
